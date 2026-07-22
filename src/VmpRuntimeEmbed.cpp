#include "VmpRuntimeEmbed.h"

namespace llvm::vllvm {
namespace {
#include "vminterpreter/VmpRuntimeBitcode.inc"
} // namespace

ArrayRef<std::uint8_t> getVmpRuntimeBitcode() {
  return ArrayRef(VmpRuntimeBitcode, sizeof(VmpRuntimeBitcode));
}

} // namespace llvm::vllvm
