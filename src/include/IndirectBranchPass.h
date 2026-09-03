#pragma once
#include "CryptoUtils.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/PassManager.h"

#include <map>
#include <memory>
#include <vector>

using namespace llvm;
class IndirectBranchPass : public PassInfoMixin<IndirectBranchPass> {
public:
  std::vector<BasicBlock *> BBTargets;
  std::map<BasicBlock *, int> BBNums;
  std::vector<BranchInst *> BrInstSites;
  std::map<BranchInst *, std::vector<BasicBlock *>> FakeBBTargets;
  std::unique_ptr<CryptoUtils> cryptoUtils;
  PreservedAnalyses run(Function &F, FunctionAnalysisManager &FAM);
  static bool isRequired() { return true; }

private:
  GlobalVariable *makeGloableBBTable(Function &F);
  bool makeIndirectBranch(Function &F, GlobalVariable *BBTableGV);
  bool getAllBBs(Function &F);
};
