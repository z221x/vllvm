#pragma once

#include "CryptoUtils.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/PassManager.h"

#include <cstdint>

using namespace llvm;

class BogusControlFlowPass : public PassInfoMixin<BogusControlFlowPass> {
public:
  PreservedAnalyses run(Function &F, FunctionAnalysisManager &FAM);
  static bool isRequired() { return true; }

private:
  bool runBogusControlFlow(Function &F);
  bool addBogusFlow(BasicBlock &BB, GlobalVariable *X, GlobalVariable *Y,
                    uint32_t XSeed, uint32_t YSeed, CryptoUtils &Crypto);
  Value *createOpaquePredicate(IRBuilder<> &IRB, GlobalVariable *X,
                               GlobalVariable *Y, uint32_t XSeed,
                               uint32_t YSeed, CryptoUtils &Crypto,
                               bool &PredicateIsTrue);
  void insertBogusJunk(IRBuilder<> &IRB, GlobalVariable *X, GlobalVariable *Y,
                       CryptoUtils &Crypto);
  void terminateFakePath(BasicBlock *Fake, BasicBlock *Tail, GlobalVariable *X,
                         GlobalVariable *Y, uint32_t XSeed, uint32_t YSeed,
                         CryptoUtils &Crypto);
};
