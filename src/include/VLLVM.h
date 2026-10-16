#pragma once

#include "llvm/IR/PassManager.h"
#include "llvm/Passes/PassBuilder.h"

namespace llvm::vllvm {
struct VLLVMOptions {
  bool EncryptoStr = false;
  bool VMFlattenFunc = false;
  bool IndirectCall = false;
  bool IndirectBranch = false;
  bool BogusControlFlow = false;
  bool Vmp = false;
  bool BB2Func = false;
  bool Merge = false;

  bool any() const {
    return EncryptoStr || VMFlattenFunc || IndirectCall ||
           IndirectBranch || BogusControlFlow || Vmp || BB2Func || Merge;
  }
};

// 混淆与优化级别不兼容（管线起点会拒绝 >O0 的标注模块），Level 由
// PassBuilder 挂钩传入。
void addVLLVMPasses(ModulePassManager &MPM, OptimizationLevel Level);
void addVLLVMLatePasses(ModulePassManager &MPM);
} // namespace llvm::vllvm
