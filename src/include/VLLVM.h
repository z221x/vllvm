#pragma once

#include "llvm/IR/PassManager.h"

namespace llvm::vllvm {
struct VLLVMOptions {
  bool EncryptoStr = false;
  bool VMFlattenFunc = false;
  bool FlattenFunc = false;
  bool IndirectCall = false;
  bool IndirectBranch = false;
  bool BogusControlFlow = false;
  bool Vmp = false;
  bool BB2Func = false;
  bool Merge = false;

  bool any() const {
    return EncryptoStr || VMFlattenFunc || FlattenFunc || IndirectCall ||
           IndirectBranch || BogusControlFlow || Vmp ||
           BB2Func || Merge;
  }
};

void addVLLVMPasses(ModulePassManager &MPM);
void addVLLVMLatePasses(ModulePassManager &MPM);
} // namespace llvm::vllvm
