#pragma once
#include "llvm/IR/Function.h"
#include "llvm/IR/PassManager.h"

using namespace llvm;
class VMFlattenFuncPass : public PassInfoMixin<VMFlattenFuncPass> {
public:
  explicit VMFlattenFuncPass(bool RunBogusControlFlow = false)
      : RunBogusControlFlow(RunBogusControlFlow) {}

  PreservedAnalyses run(Function &F, FunctionAnalysisManager &FAM);
  static bool isRequired() { return true; }

private:
  bool runVMFlattenFunc(Function &F, FunctionAnalysisManager &FAM);
  bool RunBogusControlFlow = false;
};
