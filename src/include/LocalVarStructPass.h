#pragma once
#include "llvm/IR/Function.h"
#include "llvm/IR/PassManager.h"

using namespace llvm;
class LocalVarStructPass : public PassInfoMixin<LocalVarStructPass> {
public:
  PreservedAnalyses run(Function &F, FunctionAnalysisManager &FAM);
  static bool isRequired() { return true; }

private:
  bool moveAllocasToStruct(Function &F);
};
