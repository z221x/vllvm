# VLLVM for LLVM 21.1

这个项目把已有 OLLVM 风格 Pass 集成进 LLVM/Clang 源码树，最终编译出来的是原生 `clang`，不使用 `-fpass-plugin`。

## 支持的 clang 参数

- `-enstr`: 字符串加密
- `-fla`: 控制流平坦化
- `-icall`: 间接调用
- `-ibr`: 间接跳转
- `-lvars`: 将函数局部变量搬入入口处 malloc 的结构体
- `-ollvm`: 一次启用以上全部 Pass

当 `-fla`、`-icall`、`-lvars` 同时启用时，会自动使用组合 Pass：
平坦化 case 值、间接调用加密下标、局部变量结构体偏移共用同一张
四字节 `i32` 的 `vllvm.combined.const.table.*`。间接调用的函数地址仍保留在独立
`func_table*` 指针表中。

示例：

```bash
clang -enstr test.c -o test
clang -ollvm test.c -o test
```

也可以只给指定函数启用混淆。源码里使用 Clang/GCC 兼容的
`annotate` attribute 标记函数即可，不需要额外传 `-fla` 等全局参数：

```c
#define VLLVM_OBF(kind) __attribute__((annotate("vllvm:" kind)))

VLLVM_OBF("fla,icall,lvars")
int protected_add(int a, int b) {
  return a + b;
}

VLLVM_OBF("ibr")
int protected_branch(int x) {
  return x > 0 ? x : -x;
}
```

支持的函数标记名和命令行参数一致：`enstr`、`fla`、`icall`、`ibr`、
`lvars`、`ollvm`/`all`。多个标记可以用逗号、空格、`+`、`|` 或 `;`
分隔。`enstr` 对应字符串加密，它是 Module Pass；在函数 attribute 中出现时会启用
当前编译模块的字符串加密，而不是只加密该函数内的字符串。

如果直接处理 LLVM IR，也可以写函数字符串属性：

```llvm
define i32 @protected_add(i32 %a, i32 %b) #0 {
  ret i32 %a
}

attributes #0 = { "vllvm.fla" "vllvm.icall" "vllvm.lvars" }
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
