# VLLVM for LLVM 21.1

VLLVM 将 OLLVM 风格和实验性 VMP 混淆 Pass 集成进 LLVM/Clang 源码树，构建产物是原生 `clang`，不依赖 `-fpass-plugin`。混淆通过 `annotate("vllvm:...")` 标记精确作用到函数或字符串相关对象。

## 已实现混淆

| 标记 | Pass | 说明 |
| --- | --- | --- |
| `enstr` | EncryptoStrPass | 字符串加密，Module Pass；每个字符串首次解密后缓存地址，后续复用。 |
| `fla` | FlattenFuncPass | 控制流平坦化。 |
| `lvars`（表参数化） | moveTablesToImplParams | 函数级混淆完成后把 fla 等每函数常量表改为参数传递：函数体搬进带表参数的私有 `vllvm.impl`，原函数退化为传表包装器，并以 volatile 守卫的伪调用点阻断 -O2 过程间常量传播把参数折叠回全局。indirectbr/musttail 等场景整体回退。 |
| `icall` | IndirectCallPass | 标注在被调函数上：模块内所有调用点改写为对**各自专属 naked 跳板函数**（`__vllvm_icall.<id>.call.<n>.<fn>`，签名 = 目标签名 + 隐藏 `i32 nest` 调用号）的直调；调用方先把注册号（`group_id<<16\|index`）作为调用点常量经 icallcc 的 nest 通道写入 w19（`mov w19, #imm`），跳板函数体是按目标随机生成的汇编——x0~x7 上就地应用参数的随机可逆运算链加密（仅 add/sub/xor），再用 `ubfx/and` 从 w19 解码 (group,index) 查随机注册的调用池，经 `ldr x19`/`ret x19` 跳转到真实目标（与 `src/funccaller` 的 call_func 序列一致）；池在 `init_array` 乱序注册。icallcc 约定参数/返回沿用标准 ABI 位置、CSR 掩码不含 x19。等级语义（`-mllvm -vllvm-config=icall=<n>`）：0 关闭；1（默认）仅专属跳板间接化；2 再叠加参数加密与调用点 fake 诱饵下标（过同款可逆链的假 nest 常量写入诱饵全局，淹没真实调用号）。目标入口把参数解密一次写回参数栈槽（`参数 = 解密的参数`），之后的使用全部按左值（volatile 槽位读取）进行，优化器无法把解密算术重物化回使用点。地址被取用、存在不可改写调用点（invoke/musttail/vmp 调用方等）、或非内部链接的目标不参与参数加密，仅入池。加密覆盖 x0~x7 寄存器传入的 32/64 位整数与普通地址空间指针（入口经 ptrtoint/inttoptr 还原；arm64e/PAC 目标与非零地址空间指针保持明文）；浮点（v 寄存器）、栈传参数与 8/16/128 位整数保持明文。 |
| `ibr` | IndirectBranchPass | 先将 `switch` 降为 if/else 分支，再随机使用混合、ADD、XOR、SUB 或明文模式处理下标，动态查全部非入口块的地址表（LLVM 禁止对入口块取地址）。 |
| `lvars` | （vmfla 捆绑） | 局部变量结构体化不再是独立 pass：`lvars` 标注别名到 `vmfla`，结构化只能随 vmfla 捆绑执行；字段布局随机打乱，偏移经 vmfla 共享常量表加密，不再对应源码声明顺序。 |
| `bcf` | BogusControlFlowPass | 插入基于可写全局状态和 `volatile` load 的虚假控制流。 |
| `vmfla` | VMFlattenFuncPass | VM 风格函数平坦化，共享一张整数常量表；链路为 `bb2func + merge + vmfla`，见下文。 |
| `bb2func` | BB2FuncPass | CFG 区域随机重组（线性链先合并再随机切分）后用 CodeExtractor 提取为 `noinline` 内部 helper（`vllvm.bb2f.<函数名>.<n>`），打散源码级块边界。 |
| `merge` | MergePass | 把带标记的函数按组融进 keyed dispatcher（`vllvm.merge.<组名>.<n>`）：`Hi^(Hi^编号)` 的 64 位 key 隐藏函数编号，成员体内联进 case 块后原函数退化为转发 wrapper 或被删除。 |
| `vmp` | VmpPass | 实验性 Virtual Machine Protection。 |

## vmfla 链路

`vllvm:vmfla` 不再单独执行平坦化，而是按固定顺序运行三段：

```text
bb2func（切出 helper 并打 merge 标记） -> merge（helper 融进 keyed dispatcher） -> vmfla（平坦化）
```

前两段在第一段函数级处理里把原函数打散成若干 helper 并融合进分发器，
原函数的调用点变为携带 key 常量的 dispatcher 调用；vmfla 在模块级
MergePass 之后的第二段函数级处理里执行，对包含 dispatcher 调用的函数做
最终平坦化（调用点保持直接调用，不做间接化）。单独标注
`vllvm:bb2func` 只执行提取，单独标注 `vllvm:merge` 只把该函数并入共享
dispatcher 组。bb2func/merge 遇到 EH、动态栈状态、musttail、自定义调用
约定等不支持场景时保持原函数不变。

## 构建依赖

三端统一需要 `clang`、`clang++`、`cmake`、`ninja` 和 `git`。构建脚本会应用 `patches/` 中的 LLVM 集成补丁，并同步 `src/` 到 LLVM 源码树内的 `llvm/lib/Transforms/VLLVM`。

macOS 如果工具来自 Homebrew，可先设置：

```bash
export PATH="/opt/homebrew/bin:$PATH"
```

## 构建

Linux：

```bash
NO_CLONE=1 JOBS=8 ./build-linux.sh
./build/llvm-linux/bin/clang input.c -o input
```

macOS：

```bash
NO_CLONE=1 JOBS=8 ./build-macos.sh
./build/llvm-macos/bin/clang input.c -o input
```

Windows：

```powershell
.\build-windows.ps1
.\build\llvm-windows\bin\clang.exe input.c -o input.exe
```

使用 `clang-cl` 构建 Windows 版本：

```powershell
.\build-windows.ps1 -CCompiler clang-cl -CXXCompiler clang-cl
```

如果本地没有 `llvm-project-21.1.0/`，不要设置 `NO_CLONE=1`，脚本会尝试自动 clone 对应 LLVM tag。

## 测试

构建完成后运行对应 shell 测试。常用回归入口：

```bash
./test/enstr/test_enstr.sh
./test/enstr/test_enstr_phi.sh
./test/enstr/test_enstr_cache.sh
./test/phi/test_phi_lowering.sh
./test/flatten/test_flatten.sh
./test/indirectcall/test_indirectcall.sh
./test/indirectcall/test_icall_verifier.sh
./test/indirectcall/test_icall_cross_tu.sh
./test/indirectbranch/test_indirectbr.sh
./test/localvarstruct/test_lvars.sh
./test/localvarstruct/test_lvars_fla.sh
./test/boguscontrolflow/test_bcf.sh
./test/complex/test_passes_complex.sh
```

VMP 相关测试：

```bash
./test/vmp/test_interpreter.sh
./test/vmp/test_target.sh
./test/vmp/test_vmp.sh
./test/vmp/test_runtime_sdk.sh
./test/vmp/test_fallback.sh
./test/vmp/test_cross_targets.sh
```

bb2func/merge 与 vmfla 链路测试：

```bash
./test/bb2func/test_bb2func.sh
./test/merge/test_merge.sh
./test/vmfla/test_vmfla_chain.sh
```

测试脚本会优先使用 `build/llvm-macos/bin/clang` 或 `build/llvm-linux/bin/clang`，也可通过 `CLANG=/path/to/clang` 指定编译器。
新增的 IR 独立校验脚本还需要同一构建目录中的 `llvm-as`（可通过 `LLVM_AS` 覆盖）。

开源应用级 Pass 性能基准见 `test/perf/`（zlib/lua/sqlite/bzip2 全程序标注，
测运行时/体积/编译开销与正确性），结果报告在 `docs/PASS_PERF_REPORT.md`。
Endless Tunnel 的 Android APK 构建、IR 校验和真机核心算法测试见 `test/android/README.md`。

`enstr` 为每个加密字符串生成一个 `vllvm.enstr.cache.*` 全局指针，初始为空。
首次使用时原子获取初始化权、分配内存并解密，完成后以 release/acquire 发布和
读取地址；同一字符串的不同使用点共享缓存，并发首次调用也只分配一次。
缓存命中不再分配或解密。分配失败直接触发 trap，避免发布无效地址。
缓存不自动释放，明文保留至进程退出，以维持字符串的静态生命周期；这不是
“使用后擦除”模式，调用方也不应对返回地址调用 `free`。

## 公共 PHI 降级

`Utils.h` 的 `lowerPHINodes(Function &F)` 由 `enstr`、`fla`、`vmfla`
共用：PHI 转换为入口块的 `alloca`、前驱块的 `store` 和汇合块的 `load`，
底层使用 LLVM `DemotePHIToStack`，保留循环 PHI 的并行赋值语义。
`invoke` 的 normal 边先拆出跳板，确保返回值在调用完成后才存入栈槽；
生成的访存指令保留调试位置，字符串解密调用也保留 EH funclet 上下文。

工具返回 `Unchanged`、`Lowered` 或 `Unsupported`。遇到不可存储类型、
PHI 所在块/前驱是 `catchswitch`，或 incoming 是该前驱 `callbr` 的返回值时，
整个函数保持不变。`fla/vmfla` 在改写 CFG 前回退；`enstr` 跳过仍被 PHI
引用的字符串。字符串加密先降级再收集 users，不再单独处理 PHI；
`fixStack`/`fixStackForFlatten` 复用该工具，额外处理普通跨块 SSA 值。

降级约束只针对这些 Pass 的处理阶段；后续 LCSSA、mem2reg 等 LLVM 优化
可能重新生成 PHI。`test/phi/test_phi_lowering.sh` 同时检查 Pass 后的 IR、
`-O0/-O2` 运行结果、异常传播和不支持场景的完整回退。

## 项目结构

```text
src/Pass/                Pass 实现（fla/vmfla/ibr/icall/bcf/enstr/bb2func/merge/vmp）
src/attribute/           注解解析（VLLVMAttribute）
src/common/              公共工具（Utils、CryptoUtils）
src/config/              全局混淆等级配置（VLLVMConfig）
src/include/             Pass 和公共头文件
src/c_func/vminterpreter/ VMP 解释器与 runtime bitcode 生成
src/c_func/enstrpool/    enstr 字符串池 C 运行时（mmap 匿名内存 + 下标访问）
src/c_func/funccaller/   间接调用运行时辅助
src/VMP/                 实验性 LLVM VMP target 与 runtime 嵌入
patches/                 LLVM/Clang 集成补丁
test/                    按 pass 分组的回归测试
docs/                    设计文档（含 PROGRESS.md 进度）
```

开发时优先修改 `src/` 和 `patches/`。`llvm-project-21.1.0/` 是构建工作树，VLLVM 文件由脚本同步生成，不应作为主源码维护。

## 可选安装

脚本不会主动安装。确认 build 目录中的工具可用后，可手动安装：

```bash
cmake --install build/llvm-linux --prefix /opt/vllvm
cmake --install build/llvm-macos --prefix /opt/vllvm
```

Windows：

```powershell
cmake --install .\build\llvm-windows --prefix C:\vllvm
```

## 参考资料

- [amice](https://github.com/fuqiuluo/amice)
