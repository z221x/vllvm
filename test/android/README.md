# Endless Tunnel Android 混淆测试

上游：https://github.com/android/ndk-samples

已 shallow clone 到 `test/app/ndk-samples`，版本为
`988d73bed0fd3dd362d8041a34f958be76769773`。上游 checkout 保持干净。
游戏自己的 24 个 `.cpp` 和 29 个 `.hpp` 共 6,682 行（含注释、空行，不含 GLM）。

## 测试方法

复制原生源码到本目录的 `out/source`，在游戏源码和头文件的 include 之后添加
条件注解；不修改上游源码、不注解 SDK 或 GLM。为所有游戏函数请求对应混淆，
包括头文件定义的成员函数和模板；各 Pass 的 EH/类型等限制仍然有效。

普通版和混淆版均使用本地 VLLVM Clang 21.1，以相同的 `-O2 -g` 编译，
NDK r27 提供头文件、运行库、链接器。每个 C++ 单元同时保留 `.ll` 和 `.o`，
对象由 Clang 直接编译原始源码产生，而不是再次编译已混淆 IR。
所有游戏编译单元启用 `-Xclang -llvm-verify-each`，在每个 Pass 后检查 IR；
未注解的第三方 GLM 不启用逐 Pass 校验。另用 `llvm-as` 独立校验导出的 IR。

测试使用 `arm64-v8a`、minSdk 23、targetSdk 34、静态 libc++。
由于这是不含 Java/Kotlin 的 NativeActivity 应用，直接用 CMake + aapt2 +
zipalign + apksigner 打包，不需要 Gradle，也没有下载或替换全局 SDK/NDK。
适配文件只改生成目录中的 WHOLE_ARCHIVE 链接写法，并复用 SDK 的 AndroidNdkModules。
测试 APK 使用独立包名 `com.vllvm.test.endlesstunnel.<mode>` 和本地测试签名，
保留调试符号；不是生产发行包。

## 运行

在 VLLVM 仓库根目录执行：

```bash
# 本次修补产物单独保存，保留 out/ 中的首次失败记录。
export OUT_DIR="$PWD/test/android/out-fixed"
bash test/android/build_endless_tunnel.sh
# 也可单独构建指定模式：
bash test/android/build_endless_tunnel.sh baseline ibr combined

PATH="/opt/homebrew/bin:$PATH" cmake --build build/llvm-macos --target llvm-as --parallel 8
bash test/android/verify_endless_tunnel_ir.sh

bash test/android/build_endless_tunnel_core.sh
ANDROID_SERIAL=<测试设备序列号> bash test/android/test_endless_tunnel_core.sh
```

脚本的默认工具路径适配本次 macOS 环境；可用 `ANDROID_SDK_ROOT`、
`ANDROID_NDK_ROOT`、`ANDROID_BUILD_TOOLS`、`JAVA_HOME`、`CLANG`、`LLVM_AS`、
`OUT_DIR`（绝对路径）覆盖。构建和 IR 校验存在失败时返回非零状态，继续收集其他模式。

默认单测 `enstr/fla/icall/ibr/lvars/bcf/vmfla/vmp`；`combined` 为
`enstr,bcf,lvars,fla,icall,ibr`。VMP 有优先级，VMFlatten 是另一条组合实现路径，
所以不能将 8 个标记同时开启就宣称 8 种变换都叠加生效。

## 共享 PHI 降级验证（2026-09-04，out-phi）

`enstr/fla/vmfla` 现统一使用 `Utils.h` 中声明的 `lowerPHINodes`。
旧的 enstr 专用 PHI 插入逻辑已替换；先将 PHI 降为
栈槽，再按普通指令插入解密调用。`invoke` normal 边单独拆分；不支持的
`catchswitch`/`callbr` PHI 保持原样并让相应变换回退。

本轮重新构建 `enstr/fla/vmfla/combined` 四个 APK，均通过签名校验，
96 个游戏编译单元通过逐 Pass 校验及 `llvm-as` 独立校验。
产物在 `out-phi/apk/`，构建日志、IR 和独立校验日志在 `out-phi/<mode>/`。

```bash
OUT_DIR="$PWD/test/android/out-phi" bash test/android/build_endless_tunnel.sh enstr fla vmfla combined
bash test/phi/test_phi_lowering.sh
```

新增共享回归通过 6 种模式（含 baseline）的 `-O0/-O2` 运行测试，覆盖
循环 PHI 交换、临界边、重复后继、字符串、invoke 返回值和异常传播；
另通过 6 个不支持场景的回退检查及 Windows funclet 解密调用的 IR 校验。
原有 enstr（含 PHI/cache）、lvars_fla、complex、bcf、ibr、icall、icall verifier、
VMP、runtime_sdk、interpreter、target 共 13 个脚本也全部通过。
本轮未安装或运行新 APK，下文真机结果仅适用于较早的 `out-fixed/` 产物。

## 字符串地址缓存补充验证（2026-09-04，out-cache）

`enstr` 现为每个字符串保留一个全局明文地址缓存，首次分配并解密后原子发布，
后续使用共享地址。并发初始化只允许一个线程执行 `malloc`；不自动释放，
因此明文会保留至进程退出。

本轮重新构建 `enstr` 和 `combined`，两个 APK 均通过签名校验，48 个游戏
编译单元均通过逐 Pass 校验及导出 IR 的独立校验。新产物在
`out-cache/apk/`，构建日志、IR、独立校验日志在 `out-cache/<mode>/`。
可复现命令：

```bash
OUT_DIR="$PWD/test/android/out-cache" bash test/android/build_endless_tunnel.sh enstr combined
bash test/enstr/test_enstr_cache.sh
```

新增缓存测试覆盖 `-O0/-O2`：两个字符串经单线程各 10,000 轮使用，以及
16 个线程各 10,000 轮并发使用，每个新进程都只有两次解密内存分配，
同一字符串的直接引用、全局指针引用和跨线程返回地址一致。另验证分配失败
走 trap 路径、Android/AArch64 和显式 32 位布局的 IR。
测试使用分配/失败路径钩子计数，不统计 pthread/libc 内部分配，不主动制造进程崩溃。
日志在 `test/enstr/out/cache/test.log`。

基础 enstr、PHI、lvars_fla、complex、VMP（含 LTO）、interpreter、target
这 7 个原有回归也全部通过。本轮未安装或运行新 APK；下面的真机结果属于
缓存改动前的 `out-fixed/` 产物，不能作为新版 APK 的运行验证。

## 前一轮修补结果（2026-09-04，out-fixed）

所有 10 种模式均成功构建并通过签名校验。240 个游戏编译单元全部通过
逐 Pass 校验和导出 IR 的独立校验，真机核心算法输出与普通版一致。
以下是本次 `out-fixed/` 产物；随机化会使重建后的大小、指令数略有变化。

| 模式 | APK / KiB | IR 有效 / 24 单元 | 真机原生算法回归 |
| --- | ---: | --- | --- |
| baseline | 328.4 | 24 | 通过 |
| ibr | 340.4 | 24 | 通过 |
| enstr | 360.4 | 24 | 通过 |
| fla | 380.4 | 24 | 通过 |
| icall | 324.4 | 24 | 通过 |
| lvars | 360.4 | 24 | 通过 |
| bcf | 484.4 | 24 | 通过 |
| vmfla | 472.4 | 24 | 通过 |
| vmp | 332.4 | 24 | 通过，部分函数回退 |
| combined | 920.4 | 24 | 通过 |

所有 APK 均包含 AArch64 `libgame.so`。
单独 IBR 的 IR 有 1,191 条 `indirectbr`，组合版有 15,232 条；
单独 icall 有 536 个 `call icallcc` 调用点，组合版有 1,092 个。
VMP 生成 58 个调用解释器的函数定义实例，按符号名去重为 52 个；浮点、
GEP、异常调用等不支持的函数保留原生实现，不能称为全部虚拟化。

真机型号为 Xiaomi 2201123G。原生回归复用每种 APK 构建中的
`util.cpp.o`、`obstacle.cpp.o`、`obstacle_generator.cpp.o`，测试驱动不混淆。
每种模式执行 32 个随机种子 × 18 档难度 × 100 次，共 57,600 次障碍生成；
所有进程退出码为 0，输出完全一致：

```text
cases=57600 checksum=94e0da6c09ada8d5
```

本轮只运行无界面原生算法回归，没有安装新 APK 或操作手机前台。
上轮仅普通版 APK 安装并冷启动成功；修补后的 APK 的菜单、渲染、音效、
前后台切换尚未交互验证，没有进行帧率、功耗或长时间稳定性测试。

## 根据测试修补的问题

1. `ibr`：地址表排除 LLVM 禁止取地址的入口块，入口块内的 BR 仍参与混淆。
   不改动随机索引算法、函数级状态 key、volatile 或已有的随机模式选择。
2. `icall`：在接入补丁中让 verifier 仅对自定义 `icallcc` 接受 `i32 nest`。
   保留原有 x19/w19 ABI，普通调用约定的类型规则以及自定义调用的数量、类型等
   检查仍有效；正例和 9 组反例均通过。
3. `enstr`：PHI 的解密调用移到对应前驱的终结指令前；同一前驱的重复边复用
   同一个值，避免破坏 PHI 排列和支配关系。原先崩溃的两个游戏单元现已通过。
4. `vmfla`：严格中间 IR 校验暴露了包装器与实现共享 `DISubprogram`、
   包装调用缺少调试位置和私有实现未标记 `dso_local` 的问题。调试信息随原函数体
   转移到实现函数，合成包装器不重复持有；实现函数显式采用本地绑定。
5. `vmp`：嵌入运行时带有生成时的 macOS SDK 26.2 标志，与当前 SDK 26.5
   冲突会触发后端 linker diagnostic 崩溃。移除运行时的宿主 SDK 版本，保留
   目标模块自己的版本及其他 ABI/代码生成标志；Android 不再带入 macOS SDK 标志。

另修正两个过期测试入口：enstr 的旧 Windows 命令/`-enstr` 参数改为注解驱动的
shell 回归；lvars 组合测试不再断言已移除的 `func_index_table`，改查 icallcc
调用和注册池。

以下 11 个脚本全部通过：

```text
test/enstr/test_enstr.sh
test/enstr/test_enstr_phi.sh
test/indirectbranch/test_indirectbr.sh
test/indirectcall/test_indirectcall.sh
test/indirectcall/test_icall_verifier.sh
test/localvarstruct/test_lvars_fla.sh
test/complex/test_passes_complex.sh
test/vmp/test_vmp.sh
test/vmp/test_runtime_sdk.sh
test/vmp/test_interpreter.sh
test/vmp/test_target.sh
```

覆盖 PHI 的直接字符串/指针、重复边和循环，`-O0/-O2` 的编译与运行，
`vmfla` 带调试信息的编译，以及 VMP 的 LTO、跨 SDK 和 Android IR。
最初 `out/` 中的 9 个 APK 和失败日志保留用于对照：当时 enstr 编译失败，
ibr/icall/combined 分别有 19/21/23 个 IR 被拒绝。不要将旧产物作为修补版使用。

## 产物

- `out-fixed/apk/`：修补后签名的 10 个 APK。
- `out-fixed/<mode>/build.log`：启用逐 Pass 校验的 Clang 构建日志。
- `out-fixed/<mode>/build/CMakeFiles/game.dir/*.ll`：游戏的混淆 IR。
- `out-fixed/<mode>/verify/*.log`：逐文件独立校验结果。
- `out-fixed/regression/*.log`：11 个回归脚本的运行日志。
- `out-fixed/core/*.run.log`：真机核心算法回归输出。
- `out-fixed/test.keystore`：仅供本地测试的签名文件，与旧 `out/` 的签名不同。

真机保留普通版测试包，以及 `/data/local/tmp/vllvm-endless-tunnel-988d73b`
中的回归程序；没有清除原有应用数据。没有提交或推送本次改动。
