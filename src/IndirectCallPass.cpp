#include "IndirectCallPass.h"
#include "llvm/IR/IRBuilder.h"
PreservedAnalyses IndirectCallPass::run(Function &F,
                                        FunctionAnalysisManager &FAM) {
  errs() << "[vllvm] IndirectCallPass:" << F.getName() << "\n";
  cryptoUtils = new CryptoUtils(F.getParent());
  getAllCallees(F);
  bool isChanged = makeIndirectCall(F, makeGloableFuncTable(F));
  return isChanged ? PreservedAnalyses::none() : PreservedAnalyses::all();
}

void IndirectCallPass::getAllCallees(Function &F) {
  Callees.clear();
  CalleeNums.clear();
  CallSites.clear();
  for (auto &BB : F) {
    for (auto &I : BB) {
      if (auto *callInst = dyn_cast<CallInst>(&I)) {
        Function *callee = callInst->getCalledFunction();
        if (callee == nullptr) {
          continue;
        }
        // 内联函数不做处理
        if (callee->isIntrinsic()) {
          continue;
        }
        if (callee->isDeclaration()) {
          continue;
        }
        if (callInst->isMustTailCall()) {
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
  for (size_t i = 0; i < Callees.size(); ++i) {
    CalleeNums[Callees[i]] = static_cast<int>(i);
  }
}
GlobalVariable *IndirectCallPass::makeGloableFuncTable(Function &F) {
  if (Callees.empty())
    return nullptr;

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
    funcAddr =
        ConstantExpr::getGetElementPtr(Type::getInt8Ty(Ctx), funcAddr, addKey);
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
  if (CallSites.empty() || funcTableGV == nullptr)
    return false;

  LLVMContext &Ctx = F.getContext();
  auto *funcTableTy = cast<ArrayType>(funcTableGV->getValueType());
  for (auto callInst : CallSites) {
    auto callee = callInst->getCalledFunction();
    IRBuilder IRB(callInst);
    IRB.SetInsertPoint(callInst);
    Value *funcPtr = IRB.CreateInBoundsGEP(
        funcTableTy, funcTableGV,
        {ConstantInt::get(Type::getInt32Ty(Ctx), 0),
         ConstantInt::get(Type::getInt32Ty(Ctx), CalleeNums[callee])});
    Value *funcAddr = IRB.CreateLoad(IRB.getPtrTy(), funcPtr);
    Constant *addKey = ConstantInt::get(
        Type::getInt32Ty(Ctx),
        cryptoUtils->getRandom32BaiscIndex(CalleeNums[callee]));
    funcAddr =
        IRB.CreateGEP(IRB.getInt8Ty(), funcAddr, ConstantExpr::getNeg(addKey));
    callInst->setCalledOperand(funcAddr);
  }
  return true;
}
