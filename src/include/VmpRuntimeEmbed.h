#pragma once

#include "llvm/ADT/ArrayRef.h"

#include <cstdint>

namespace llvm::vllvm {

ArrayRef<std::uint8_t> getVmpRuntimeBitcode();

} // namespace llvm::vllvm
