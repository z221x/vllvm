#include "IndirectCallPass.h"
#include "llvm/IR/IRBuilder.h"
PreservedAnalyses IndirectCallPass::run(Function &F,
                                        FunctionAnalysisManager &FAM) {
  errs() << "[vllvm] IndirectCallPass:" << F.getName() << "\n";
  bool isChanged = false;
  cryptoUtils = new CryptoUtils(F.getParent());
  getAllCallees(F);
  isChanged = makeIndirectCall(F, makeGloableFuncTable(F));
  return isChanged ? PreservedAnalyses::none() : PreservedAnalyses::all();
}

void IndirectCallPass::getAllCallees(Function &F) {
  Callees.clear();
  CalleeNums.clear();
  CallSites.clear();
  for (auto &BB : F) {
    for (auto &I : BB) {
      if (dyn_cast<CallInst>(&I)) {
        auto *callInst = dyn_cast<CallInst>(&I);
        Function *callee = callInst->getCalledFunction();
        // 内联函数不做处理
        if (callee->isIntrinsic()) {
          continue;
        }
        if (callee == nullptr) {
          continue;
        }
        if (callee->isDeclaration()) {
          continue;
        }
        CallSites.push_back(callInst);
        if (std::find(Callees.begin(), Callees.end(), callee) ==
            Callees.end()) {
          Callees.push_back(callee);
        }
        if (CalleeNums.count(callee) == 0) {
          CalleeNums[callee] = 0;
        }
      }
    }
  }
  long seed = cryptoUtils->getRandom32();
  std::default_random_engine e(seed);
  std::shuffle(Callees.begin(), Callees.end(), e);
  for (int i = 0; i < Callees.size(); i++) {
    CalleeNums[Callees[i]] = i;
  }
}
GlobalVariable *IndirectCallPass::makeGloableFuncTable(Function &F) {
  Module *M = F.getParent();
  LLVMContext &Ctx = M->getContext();
  // 创建全局数组存储函数加密后地址
  Type *voidPtrTy = PointerType::getUnqual(Ctx);
  ArrayType *funcTableTy = ArrayType::get(voidPtrTy, Callees.size());
  std::vector<Constant *> funcPtrs;
  for (auto callee : Callees) {
    Constant *funcAddr = ConstantExpr::getBitCast(callee, voidPtrTy);
    Constant *addKey = ConstantInt::get(
        Type::getInt32Ty(Ctx),
        cryptoUtils->getRandom32BaiscIndex(CalleeNums[callee]));
    // 对地址进行add操作进行加密
    funcAddr = ConstantExpr::getGetElementPtr(voidPtrTy, funcAddr, addKey);
    funcPtrs.push_back(funcAddr);
  }
  Constant *funcTableInit = ConstantArray::get(funcTableTy, funcPtrs);
  GlobalVariable *funcTableGV =
      new GlobalVariable(*M, funcTableTy, true, GlobalValue::PrivateLinkage,
                         funcTableInit, "func_table" + F.getName());
  return funcTableGV;
}
bool IndirectCallPass::makeIndirectCall(Function &F,
                                        GlobalVariable *funcTableGV) {
  Module *M = F.getParent();
  LLVMContext &Ctx = M->getContext();
  for (auto callInst : CallSites) {
    auto callee = callInst->getCalledFunction();
    IRBuilder IRB(callInst);
    IRB.SetInsertPoint(callInst);
    Value *funcPtr = IRB.CreateGEP(
        Type::getInt64Ty(Ctx), funcTableGV,
        ConstantInt::get(Type::getInt64Ty(Ctx), CalleeNums[callee]));
    Value *funcAddr = IRB.CreateLoad(IRB.getPtrTy(), funcPtr);
    Constant *addKey = ConstantInt::get(
        Type::getInt32Ty(Ctx),
        cryptoUtils->getRandom32BaiscIndex(CalleeNums[callee]));
    funcAddr =
        IRB.CreateGEP(IRB.getInt64Ty(), funcAddr, ConstantExpr::getNeg(addKey));
    callInst->setCalledOperand(funcAddr);
  }
  return true;
}