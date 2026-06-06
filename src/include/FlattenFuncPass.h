#pragma once
#include "llvm/IR/Function.h"
#include "llvm/IR/PassManager.h"
#include "llvm/Pass.h"
using namespace llvm;
class FlattenFuncPass : public PassInfoMixin<FlattenFuncPass> {
public:
  PreservedAnalyses run(Function &F, FunctionAnalysisManager &FAM);
  bool doFlatten(Function &F, FunctionAnalysisManager &FAM);
  static bool isRequired() { return true; }
};