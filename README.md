# VLLVM for LLVM 21.1

这个项目把已有 OLLVM 风格 Pass 集成进 LLVM/Clang 源码树，最终编译出来的是原生 `clang`，不使用 `-fpass-plugin`。

## 函数标记

VLLVM 不提供命令行开关。需要混淆的函数必须使用 Clang/GCC 兼容的
`annotate` attribute 标记：

```c
#define VLLVM_OBF(kind) __attribute__((annotate("vllvm:" kind)))

VLLVM_OBF("fop")
int protected_function(int a, int b) {
  return a + b;
}

VLLVM_OBF("ibr")
int protected_branch(int x) {
  return x > 0 ? x : -x;
}

VLLVM_OBF("ibr,icall,fla")
int protected_mixed_primitives(int x) {
  return protected_branch(x) + 1;
}

VLLVM_OBF("bcf")
int protected_bogus_flow(int x) {
  return x * 3 + 1;
}
```

支持的标记名：`enstr`、`fop`、`fla`、`icall`、`ibr`、`lvars`、`bcf`。
多个标记可以用逗号、空格、`+`、`|` 或 `;` 分隔，顺序不敏感。

`bcf` 对应虚假控制流，会为可安全拆分的基本块插入基于私有可写全局状态和
`volatile` load 的不透明谓词、真实后继和不会被执行的 fake 分支。它会保守跳过
异常处理、`musttail`、`callbr`、`indirectbr` 等 ABI 或 CFG 敏感场景。

`fop` 是独立的函数级混淆 Pass，不再由 `fla`、`icall`、`lvars` 三个标记自动组合触发。
它会让平坦化 case 值、间接调用加密下标和 key、局部变量结构体偏移和 key 共用同一张
四字节 `i32` 的 `vllvm.fop.const.table.*`。flatten 运行时状态保存的是当前
case 值在这张表里的下标，icall/lvars 的 key 读取也会基于这个变化的下标派生。
`fop` 的实际改写顺序是 `icall` -> `lvars` -> `fla`。
如果同时启用 `bcf`，虚假控制流的不透明谓词种子和 junk 常量也会先写入这张表，
再把混淆后的函数体搬进 `*.vllvm.impl`。间接调用的函数地址仍保留在独立
`func_table*` 指针表中。单独标记 `fla`、`icall`、`lvars` 时仍分别运行对应的独立 Pass。

`enstr` 对应字符串加密，它是 Module Pass；在函数 attribute 中出现时会启用当前
编译模块的字符串加密，而不是只加密该函数内的字符串。

如果直接处理 LLVM IR，也可以写函数字符串属性：

```llvm
define i32 @protected_add(i32 %a, i32 %b) #0 {
  ret i32 %a
}

attributes #0 = { "vllvm.fop" }
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
./build/llvm-linux/bin/clang input.c -o input
```

## macOS

```bash
./build-macos.sh
```

编译完成后直接使用：

```bash
./build/llvm-macos/bin/clang input.c -o input
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
.\build\llvm-windows\bin\clang.exe input.c -o input.exe
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
