# VLLVM for LLVM 21.1

VLLVM 将 OLLVM 风格和实验性 VMP 混淆 Pass 集成进 LLVM/Clang 源码树，构建产物是原生 `clang`，不依赖 `-fpass-plugin`。混淆通过 `annotate("vllvm:...")` 标记精确作用到函数或字符串相关对象。

## 已实现混淆

| 标记 | Pass | 说明 |
| --- | --- | --- |
| `enstr` | EncryptoStrPass | 字符串加密，Module Pass；每个字符串首次解密后缓存地址，后续复用。 |
| `fla` | FlattenFuncPass | 控制流平坦化。 |
| `icall` | IndirectCallPass | AArch64 模块级调用表随机注册，通过 `icallcc`/`x19` 查表跳转。 |
| `ibr` | IndirectBranchPass | 先将 `switch` 降为 if/else 分支，再随机使用混合、ADD、XOR、SUB 或明文模式处理下标，动态查全部非入口块的地址表（LLVM 禁止对入口块取地址）。 |
| `lvars` | LocalVarStructPass | 局部变量结构体化和偏移加密。 |
| `bcf` | BogusControlFlowPass | 插入基于可写全局状态和 `volatile` load 的虚假控制流。 |
| `vmfla` | VMFlattenFuncPass | 独立的 VM 风格函数平坦化，共享一张整数常量表。 |
| `vmp` | VmpPass | 实验性 Virtual Machine Protection。 |

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

测试脚本会优先使用 `build/llvm-macos/bin/clang` 或 `build/llvm-linux/bin/clang`，也可通过 `CLANG=/path/to/clang` 指定编译器。
新增的 IR 独立校验脚本还需要同一构建目录中的 `llvm-as`（可通过 `LLVM_AS` 覆盖）。
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
src/                 VLLVM Pass 主源码
src/include/         Pass 和公共头文件
src/vminterpreter/   VMP 解释器与 runtime bitcode 生成
src/vmtarget/VMP/    实验性 LLVM VMP target
patches/             LLVM/Clang 集成补丁
test/                按 pass 分组的回归测试
docs/                设计文档
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
