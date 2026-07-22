#pragma once
#include "llvm/IR/Module.h"
#include "llvm/IR/PassManager.h"

namespace llvm::vllvm {
class VmpPass : public PassInfoMixin<VmpPass> {
public:
  PreservedAnalyses run(Module &M, ModuleAnalysisManager &MAM);
  static bool isRequired() { return true; }
};
} // namespace llvm::vllvm
