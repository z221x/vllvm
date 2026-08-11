#pragma once

#include "llvm/IR/PassManager.h"

namespace llvm::vllvm {
struct VLLVMOptions {
  bool EncryptoStr = false;
  bool VMFlattenFunc = false;
  bool FlattenFunc = false;
  bool IndirectCall = false;
  bool IndirectBranch = false;
  bool LocalVarStruct = false;
  bool BogusControlFlow = false;
  bool Vmp = false;

  bool any() const {
    return EncryptoStr || VMFlattenFunc || FlattenFunc || IndirectCall ||
           IndirectBranch || LocalVarStruct || BogusControlFlow || Vmp;
  }
};

void addVLLVMPasses(ModulePassManager &MPM);
void addVLLVMLatePasses(ModulePassManager &MPM);
} // namespace llvm::vllvm
