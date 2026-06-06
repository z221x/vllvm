
#include "IndirectBranchPass.h"
#include "llvm/IR/Constant.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/IRBuilder.h"
PreservedAnalyses IndirectBranchPass::run(Function &F,
                                          FunctionAnalysisManager &FAM) {
  errs() << "[vllvm] IndirectBranchPass:" << F.getName() << "\n";
  bool isChanged = false;
  cryptoUtils = new CryptoUtils(F.getParent());
  getAllBBs(F);
  isChanged = makeIndirectBranch(F, makeGloableBBTable(F));
  return isChanged ? PreservedAnalyses::none() : PreservedAnalyses::all();
}
void IndirectBranchPass::getAllBBs(Function &F) {
  BBTargets.clear();
  BBNums.clear();
  BrInstSites.clear();
  for (BasicBlock &BB : F) {
    if (auto *BI = dyn_cast<BranchInst>(BB.getTerminator())) {
      BrInstSites.push_back(BI);
      if (BI->isConditional()) {
        unsigned N = BI->getNumSuccessors();
        for (unsigned I = 0; I < N; I++) {
          BasicBlock *Succ = BI->getSuccessor(I);
          if (BBNums.count(Succ) == 0) {
            BBTargets.push_back(Succ);
            BBNums[Succ] = 0;
          }
        }
      } else {
        BasicBlock *Succ = BI->getSuccessor(0);
        if (BBNums.count(Succ) == 0) {
          BBTargets.push_back(Succ);
          BBNums[Succ] = 0;
        }
      }
    }
  }
  long seed = cryptoUtils->getRandom32();
  std::default_random_engine e(seed);
  std::shuffle(BBTargets.begin(), BBTargets.end(), e);
  for (int i = 0; i < BBTargets.size(); i++) {
    BBNums[BBTargets[i]] = i;
  }
}
GlobalVariable *IndirectBranchPass::makeGloableBBTable(Function &F) {
  std::vector<Constant *> BBArray;
  Module *M = F.getParent();
  LLVMContext &Ctx = F.getContext();
  for (BasicBlock *BB : BBTargets) {
    Constant *enBBAddr = ConstantExpr::getBitCast(BlockAddress::get(BB),
                                                  PointerType::getUnqual(Ctx));
    enBBAddr = ConstantExpr::getGetElementPtr(
        Type::getInt8Ty(Ctx), enBBAddr,
        ConstantInt::get(Type::getInt32Ty(Ctx),
                         cryptoUtils->getRandom32BaiscIndex(BBNums[BB])));
    BBArray.push_back(enBBAddr);
  }
  ArrayType *ATy =
      ArrayType::get(PointerType::getUnqual(F.getContext()), BBArray.size());
  return new GlobalVariable(*M, ATy, false, GlobalValue::PrivateLinkage,
                            ConstantArray::get(ATy, BBArray));
}
bool IndirectBranchPass::makeIndirectBranch(Function &F,
                                            GlobalVariable *BBTableGV) {
  LLVMContext &Ctx = F.getContext();
  for (BranchInst *BI : BrInstSites) {
    if (BI->isConditional()) {
      unsigned N = BI->getNumSuccessors();
      IRBuilder<> IRB(BI);
      IRB.SetInsertPoint(BI);
      Value *Cond = BI->getCondition();
      Constant *TaddKey = ConstantInt::get(
          Type::getInt32Ty(Ctx),
          cryptoUtils->getRandom32BaiscIndex(BBNums[BI->getSuccessor(0)]));
      Constant *FaddKey = ConstantInt::get(
          Type::getInt32Ty(Ctx),
          cryptoUtils->getRandom32BaiscIndex(BBNums[BI->getSuccessor(1)]));
      Value *addKey = IRB.CreateSelect(Cond, TaddKey, FaddKey);
      Value *TIdx =
          ConstantInt::get(IRB.getInt32Ty(), BBNums[BI->getSuccessor(0)]);
      Value *FIdx =
          ConstantInt::get(IRB.getInt32Ty(), BBNums[BI->getSuccessor(1)]);
      Value *Idx = IRB.CreateSelect(Cond, TIdx, FIdx);
      // 计算目标地址
      Value *BBPtr = IRB.CreateGEP(Type::getInt64Ty(Ctx), BBTableGV, Idx);
      Value *BBAddr = IRB.CreateLoad(IRB.getPtrTy(), BBPtr);
      addKey = IRB.CreateNeg(addKey);
      BBAddr = IRB.CreateGEP(IRB.getInt8Ty(), BBAddr, addKey);
      // 创建间接跳转指令
      IndirectBrInst *indBr =
          IRB.CreateIndirectBr(BBAddr, N /*num of destinations*/);
      for (unsigned I = 0; I < N; I++) {
        BasicBlock *Succ = BI->getSuccessor(I);
        indBr->addDestination(Succ);
      }
      // 删除原有的直接跳转指令
      BI->eraseFromParent();
    } else {
      BasicBlock *Succ = BI->getSuccessor(0);
      IRBuilder<> IRB(BI);
      IRB.SetInsertPoint(BI);
      // 计算目标地址
      Value *BBPtr =
          IRB.CreateGEP(Type::getInt64Ty(Ctx), BBTableGV,
                        ConstantInt::get(Type::getInt64Ty(Ctx), BBNums[Succ]));
      Value *BBAddr = IRB.CreateLoad(IRB.getPtrTy(), BBPtr);
      Constant *addKey =
          ConstantInt::get(Type::getInt32Ty(Ctx),
                           cryptoUtils->getRandom32BaiscIndex(BBNums[Succ]));
      BBAddr =
          IRB.CreateGEP(IRB.getInt8Ty(), BBAddr, ConstantExpr::getNeg(addKey));
      // 创建间接跳转指令
      IndirectBrInst *indBr =
          IRB.CreateIndirectBr(BBAddr, 1 /*num of destinations*/);
      indBr->addDestination(Succ);
      // 删除原有的直接跳转指令
      BI->eraseFromParent();
    }
  }
  return true;
}
