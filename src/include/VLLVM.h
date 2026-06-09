#pragma once

#include "llvm/IR/PassManager.h"

namespace llvm::vllvm {
struct VLLVMOptions {
  bool EncryptoStr = false;
  bool FlattenFunc = false;
  bool IndirectCall = false;
  bool IndirectBranch = false;
  bool LocalVarStruct = false;

  bool any() const {
    return EncryptoStr || FlattenFunc || IndirectCall || IndirectBranch ||
           LocalVarStruct;
  }
};

void addVLLVMPasses(ModulePassManager &MPM, const VLLVMOptions &Options);
} // namespace llvm::vllvm
