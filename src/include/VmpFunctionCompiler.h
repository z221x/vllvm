#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace llvm {

class Function;

namespace vllvm {

inline constexpr char kVMPDataLayout[] =
    "e-m:e-p:64:64-i64:64-n8:16:32:64-S128";

// VMP 核心编译结果。Code 和 ValueTable 均为宿主无关的 64 位逻辑值；
// 只有 llc 文件输出路径会再将它们序列化为小端 VMPC 字节流。
struct VMPCodegenResult {
  std::vector<std::uint64_t> Code;
  std::vector<std::uint64_t> ValueTable;
  std::uint32_t FrameSize = 0;
};

// 将单个、已完成合法化和 HOSTCALL lowering 的 LLVM Function 直接编译为
// VMP 指令。该类不创建 TargetMachine，也不序列化 VMPC。
class FunctionCompiler {
public:
  explicit FunctionCompiler(Function &F);

  [[nodiscard]] bool compile(VMPCodegenResult &Output, std::string &Error);

private:
  Function &F;
};

} // namespace vllvm
} // namespace llvm
