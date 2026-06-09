#pragma once
#include "CryptoUtils.h"
#include "map"
#include <cstdint>
#include <vector>
#include "llvm/IR/Function.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/PassManager.h"

using namespace llvm;
class IndirectCallPass : public PassInfoMixin<IndirectCallPass> {
public:
  std::vector<Function *> Callees;
  std::map<Function *, int> CalleeNums;
  std::vector<CallInst *> CallSites;
  std::map<CallInst *, uint32_t> CallSiteIndexKeys;
  CryptoUtils *cryptoUtils;
  PreservedAnalyses run(Function &F, FunctionAnalysisManager &FAM);
  static bool isRequired() { return true; }

private:
  GlobalVariable *makeGloableFuncTable(Function &F);
  GlobalVariable *makeGloableIndexTable(Function &F);
  bool makeIndirectCall(Function &F, GlobalVariable *funcTableGV,
                        GlobalVariable *indexTableGV);
  void getAllCallees(Function &F);
};
