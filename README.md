# VLLVM for LLVM 21.1

VLLVM 将 OLLVM 风格和实验性 VMP 混淆 Pass 集成进 LLVM/Clang 源码树，构建产物是原生 `clang`，不依赖 `-fpass-plugin`。混淆通过 `annotate("vllvm:...")` 标记精确作用到函数或字符串相关对象。

## 已实现混淆

| 标记 | Pass | 说明 |
| --- | --- | --- |
| `enstr` | EncryptoStrPass | 字符串加密，Module Pass。 |
| `fla` | FlattenFuncPass | 控制流平坦化。 |
| `icall` | IndirectCallPass | AArch64 模块级调用表随机注册，通过 `icallcc`/`x19` 查表跳转。 |
| `ibr` | IndirectBranchPass | 通过可写 `volatile` 索引动态访问完整块地址常量表，并为每个 `br` 随机加入 1-2 个函数内假目标。 |
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
./test/flatten/test_flatten.sh
./test/indirectcall/test_indirectcall.sh
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
./test/vmp/test_fallback.sh
./test/vmp/test_cross_targets.sh
```

测试脚本会优先使用 `build/llvm-macos/bin/clang` 或 `build/llvm-linux/bin/clang`，也可通过 `CLANG=/path/to/clang` 指定编译器。

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
