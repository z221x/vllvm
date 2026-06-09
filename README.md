# VLLVM for LLVM 21.1

这个项目把已有 OLLVM 风格 Pass 集成进 LLVM/Clang 源码树，最终编译出来的是原生 `clang`，不使用 `-fpass-plugin`。

## 支持的 clang 参数

- `-enstr`: 字符串加密
- `-fla`: 控制流平坦化
- `-icall`: 间接调用
- `-ibr`: 间接跳转
- `-lvars`: 将函数局部变量搬入入口处 malloc 的结构体
- `-ollvm`: 一次启用以上全部 Pass

示例：

```bash
clang -enstr test.c -o test
clang -ollvm test.c -o test
```

## 构建依赖

三端统一使用 `clang`、`clang++` 和 `ninja` 作为构建环境，并需要 `cmake`、`git`。脚本会同时构建 `clang` 和同版本 `clangd`。

Windows 如果使用 LLVM/MinGW 风格 clang，默认参数即可；如果你要用 MSVC ABI，可以显式传入 `clang-cl`。

## Linux

```bash
./build-linux.sh
```

编译完成后直接使用：

```bash
./build/llvm-linux/bin/clang -enstr input.c -o input
```

## macOS

```bash
./build-macos.sh
```

编译完成后直接使用：

```bash
./build/llvm-macos/bin/clang -enstr input.c -o input
```

## Windows

```powershell
.\build-windows.ps1
```

使用 `clang-cl` 构建时：

```powershell
.\build-windows.ps1 -CCompiler clang-cl -CXXCompiler clang-cl
```

编译完成后直接使用：

```powershell
.\build\llvm-windows\bin\clang.exe -enstr input.c -o input.exe
```

## 可选安装

脚本不会主动安装。确认 build 目录里的 clang 可用后，再按需要手动安装：

```bash
cmake --install build/llvm-linux --prefix /opt/vllvm
cmake --install build/llvm-macos --prefix /opt/vllvm
```

Windows：

```powershell
cmake --install .\build\llvm-windows --prefix C:\vllvm
```

默认安装前缀只是写进 CMake 配置，不会在脚本中执行安装。可以通过 `LLVM_INSTALL_PREFIX` 或 `-LLVMInstallPrefix` 改掉这个默认值。
