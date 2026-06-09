#include "IndirectCallPass.h"
#include "llvm/ADT/Twine.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/IRBuilder.h"

#include <algorithm>
#include <random>

using namespace llvm;

namespace {
uint32_t makeNonZeroKey(CryptoUtils &Crypto, size_t CallSiteNo) {
  uint32_t Key = Crypto.getRandom32();
  if (Key != 0)
    return Key;

  Key = Crypto.getRandom32();
  if (Key != 0)
    return Key;

  return 0xA5A5A5A5U ^ static_cast<uint32_t>(CallSiteNo + 1);
}
} // namespace

PreservedAnalyses IndirectCallPass::run(Function &F,
                                        FunctionAnalysisManager &FAM) {
  errs() << "[vllvm] IndirectCallPass:" << F.getName() << "\n";
  cryptoUtils = new CryptoUtils(F.getParent());
  getAllCallees(F);
  GlobalVariable *FuncTableGV = makeGloableFuncTable(F);
  GlobalVariable *IndexTableGV = makeGloableIndexTable(F);
  bool isChanged = makeIndirectCall(F, FuncTableGV, IndexTableGV);
  return isChanged ? PreservedAnalyses::none() : PreservedAnalyses::all();
}

void IndirectCallPass::getAllCallees(Function &F) {
  Callees.clear();
  CalleeNums.clear();
  CallSites.clear();
  CallSiteIndexKeys.clear();
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
  // 创建全局数组存储当前函数内所有可改写直接调用的函数地址。
  Type *voidPtrTy = PointerType::getUnqual(Ctx);
  ArrayType *funcTableTy = ArrayType::get(voidPtrTy, Callees.size());
  std::vector<Constant *> funcPtrs;
  for (auto callee : Callees) {
    Constant *funcAddr = ConstantExpr::getBitCast(callee, voidPtrTy);
    funcPtrs.push_back(funcAddr);
  }
  Constant *funcTableInit = ConstantArray::get(funcTableTy, funcPtrs);
  GlobalVariable *funcTableGV =
      new GlobalVariable(*M, funcTableTy, true, GlobalValue::PrivateLinkage,
                         funcTableInit,
                         (Twine("func_table") + F.getName()).str());
  return funcTableGV;
}

GlobalVariable *IndirectCallPass::makeGloableIndexTable(Function &F) {
  if (CallSites.empty())
    return nullptr;

  Module *M = F.getParent();
  LLVMContext &Ctx = M->getContext();
  Type *IndexTy = Type::getInt32Ty(Ctx);
  ArrayType *IndexTableTy = ArrayType::get(IndexTy, CallSites.size());
  std::vector<Constant *> EncryptedIndexes;

  for (size_t CallSiteNo = 0; CallSiteNo < CallSites.size(); ++CallSiteNo) {
    CallInst *CallSite = CallSites[CallSiteNo];
    Function *Callee = CallSite->getCalledFunction();
    uint32_t Key = makeNonZeroKey(*cryptoUtils, CallSiteNo);
    uint32_t EncryptedIndex =
        static_cast<uint32_t>(CalleeNums[Callee]) ^ Key;
    CallSiteIndexKeys[CallSite] = Key;
    EncryptedIndexes.push_back(ConstantInt::get(IndexTy, EncryptedIndex));
  }

  Constant *IndexTableInit =
      ConstantArray::get(IndexTableTy, EncryptedIndexes);
  return new GlobalVariable(*M, IndexTableTy, false,
                            GlobalValue::PrivateLinkage, IndexTableInit,
                            (Twine("func_index_table") + F.getName()).str());
}

bool IndirectCallPass::makeIndirectCall(Function &F,
                                        GlobalVariable *funcTableGV,
                                        GlobalVariable *indexTableGV) {
  if (CallSites.empty() || funcTableGV == nullptr || indexTableGV == nullptr)
    return false;

  LLVMContext &Ctx = F.getContext();
  auto *funcTableTy = cast<ArrayType>(funcTableGV->getValueType());
  auto *indexTableTy = cast<ArrayType>(indexTableGV->getValueType());
  Type *IndexTy = indexTableTy->getElementType();
  for (size_t CallSiteNo = 0; CallSiteNo < CallSites.size(); ++CallSiteNo) {
    CallInst *callInst = CallSites[CallSiteNo];
    IRBuilder IRB(callInst);
    IRB.SetInsertPoint(callInst);
    Value *IndexPtr = IRB.CreateInBoundsGEP(
        indexTableTy, indexTableGV,
        {ConstantInt::get(Type::getInt32Ty(Ctx), 0),
         ConstantInt::get(Type::getInt32Ty(Ctx), CallSiteNo)});
    LoadInst *EncryptedIndex =
        IRB.CreateLoad(IndexTy, IndexPtr, "vllvm.icall.enc_index");
    EncryptedIndex->setVolatile(true);
    Value *Index = IRB.CreateXor(
        EncryptedIndex,
        ConstantInt::get(IndexTy, CallSiteIndexKeys[callInst]),
        "vllvm.icall.index");
    Value *funcPtr = IRB.CreateInBoundsGEP(
        funcTableTy, funcTableGV,
        {ConstantInt::get(Type::getInt32Ty(Ctx), 0), Index});
    Value *funcAddr =
        IRB.CreateLoad(IRB.getPtrTy(), funcPtr, "vllvm.icall.func");
    callInst->setCalledOperand(funcAddr);
  }
  return true;
}
