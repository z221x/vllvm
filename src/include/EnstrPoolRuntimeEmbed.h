#pragma once

#include "llvm/ADT/ArrayRef.h"

#include <cstdint>

namespace llvm::vllvm {

// enstr 字符串池 C 运行时（src/c_func/enstrpool/enstrpool.c）的位码。
ArrayRef<std::uint8_t> getEnstrPoolRuntimeBitcode();

} // namespace llvm::vllvm
