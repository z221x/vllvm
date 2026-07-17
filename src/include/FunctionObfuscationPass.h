#pragma once
#include "llvm/IR/Function.h"
#include "llvm/IR/PassManager.h"

using namespace llvm;
class FunctionObfuscationPass
    : public PassInfoMixin<FunctionObfuscationPass> {
public:
  explicit FunctionObfuscationPass(bool RunBogusControlFlow = false)
      : RunBogusControlFlow(RunBogusControlFlow) {}

  PreservedAnalyses run(Function &F, FunctionAnalysisManager &FAM);
  static bool isRequired() { return true; }

private:
  bool runFOP(Function &F, FunctionAnalysisManager &FAM);
  bool RunBogusControlFlow = false;
};
