#pragma once

#include "llvm/IR/Function.h"
#include "llvm/IR/PassManager.h"

using namespace llvm;

class BogusControlFlowPass : public PassInfoMixin<BogusControlFlowPass> {
public:
  PreservedAnalyses run(Function &F, FunctionAnalysisManager &FAM);
  static bool isRequired() { return true; }
};
