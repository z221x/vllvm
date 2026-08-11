# Repository Guidelines

## 项目结构与模块组织

`src/` 是 VLLVM 的主源码目录。Pass 头文件在 `src/include/`，VM 解释器和运行时 bitcode 生成逻辑在 `src/vminterpreter/`，实验性 VMP target 在 `src/vmtarget/VMP/`。`test/` 按 pass 分组存放 shell 回归测试，例如 `test/complex/`、`test/localvarstruct/`、`test/vmp/`。`docs/` 保存设计说明，`patches/` 保存接入 LLVM 21.1 的补丁。

优先修改 `src/` 和 `patches/`。`llvm-project-21.1.0/` 是构建用 LLVM 源码树，构建脚本会把 VLLVM 文件同步进去，不应把其中的 VLLVM 拷贝当作主源码编辑。

## 构建、测试与开发命令

- `NO_CLONE=1 JOBS=8 ./build-macos.sh`：使用现有 LLVM checkout 构建 macOS 版本。
- `NO_CLONE=1 JOBS=8 ./build-linux.sh`：构建 Linux 版本。
- `.\build-windows.ps1`：Windows 构建入口。
- `./build/llvm-macos/bin/clang input.c -o input`：使用本地构建出的 Clang 编译样例。
- `git diff --check`：提交前检查补丁空白和格式问题。
- 代码编写要必须最小化实现目标功能、并记录注释

macOS 如果 `cmake` 或 `ninja` 来自 Homebrew，可使用 `PATH="/opt/homebrew/bin:$PATH"`。

## 代码风格与命名约定

遵循 LLVM C++ 风格：两空格缩进，括号和 include 顺序参考相邻代码，类名使用 `PascalCase`，局部 helper 沿用现有 `camelCase` 风格。修改 pass 时要同步更新 pass 名、attribute 字符串、全局表名、测试断言和 README 示例。例如：`VMFlattenFuncPass`、`vllvm.vmfla`、`vllvm.vmfla.const.table.*`。

保持改动聚焦。除非能直接降低风险或减少重复，否则不要在行为改动里混入大范围重构。

## 测试指南

测试主要是可执行 shell 脚本：调用构建出的 VLLVM Clang 编译 C/C++ fixture，并检查生成的 LLVM IR。先跑最相关脚本，再跑更宽覆盖：

- `./test/localvarstruct/test_lvars_fla.sh`
- `./test/complex/test_passes_complex.sh`
- `./test/vmp/test_vmp.sh`
- `./test/vmp/test_interpreter.sh`
- `./test/vmp/test_target.sh`

新增测试应放在对应 `test/<area>/` 目录，命名体现 pass 或行为。改 transformation 时应加入 IR 断言，覆盖生成的 globals、attributes、控制流形态和回退行为。

## Commit 与 Pull Request 规范

历史提交使用简短直接的中英文标题，例如 `新增VmpPass`、`Support global string encryption annotations`。每个 commit 聚焦一个逻辑改动，标题尽量包含受影响的 pass 或功能。

PR 需要说明行为变化、目标平台、已运行的测试命令，以及仍不支持或会 fallback 的场景。有关联 issue 时一并链接。
