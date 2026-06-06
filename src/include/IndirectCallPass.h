#pragma once
#include "CryptoUtils.h"
#include "map"
#include "llvm/IR/Function.h"
#include "llvm/IR/PassManager.h"

using namespace llvm;
class IndirectCallPass : public PassInfoMixin<IndirectCallPass> {
public:
  std::vector<Function *> Callees;
  std::map<Function *, int> CalleeNums;
  std::vector<CallInst *> CallSites;
  CryptoUtils *cryptoUtils;
  PreservedAnalyses run(Function &F, FunctionAnalysisManager &FAM);
  static bool isRequired() { return true; }

private:
  GlobalVariable *makeGloableFuncTable(Function &F);
  bool makeIndirectCall(Function &F, GlobalVariable *funcTableGV);
  void getAllCallees(Function &F);
};