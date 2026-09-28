#include "EnstrPoolRuntimeEmbed.h"

namespace llvm::vllvm {
namespace {
// 借助 LLVMVLLVM 的根 include 路径解析到同步后的位码数组。
#include "c_func/enstrpool/EnstrPoolBitcode.inc"
} // namespace

ArrayRef<std::uint8_t> getEnstrPoolRuntimeBitcode() {
  return ArrayRef(EnstrPoolBitcode, sizeof(EnstrPoolBitcode));
}

} // namespace llvm::vllvm
