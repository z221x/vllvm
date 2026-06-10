#pragma once
#include "llvm/IR/Function.h"
#include "llvm/IR/PassManager.h"

using namespace llvm;
class FunctionObfuscationPass
    : public PassInfoMixin<FunctionObfuscationPass> {
public:
  PreservedAnalyses run(Function &F, FunctionAnalysisManager &FAM);
  static bool isRequired() { return true; }

private:
  bool runCombined(Function &F, FunctionAnalysisManager &FAM);
};
