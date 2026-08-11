#pragma once

#include "llvm/IR/PassManager.h"

class IndirectCallPass : public llvm::PassInfoMixin<IndirectCallPass> {
public:
  llvm::PreservedAnalyses run(llvm::Module &M,
                              llvm::ModuleAnalysisManager &MAM);
  static bool isRequired() { return true; }
};
