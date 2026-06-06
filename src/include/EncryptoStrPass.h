#pragma once
#include "llvm/IR/Function.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/PassManager.h"
#include "llvm/Pass.h"
using namespace llvm;
class EncryptoStrPass : public PassInfoMixin<EncryptoStrPass> {
public:
  class EncryptoStr;
  std::vector<EncryptoStr *> makeEncryptoStrPool(Module &M);
  PreservedAnalyses run(Module &M, ModuleAnalysisManager &MAM);
  static bool isRequired() { return true; }
};