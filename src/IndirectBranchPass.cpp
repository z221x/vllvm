#include "IndirectBranchPass.h"
#include "Utils.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/IR/Constant.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/Transforms/Utils/LowerSwitch.h"

#include <algorithm>
#include <random>

namespace {
BranchInst *getSupportedBranchTerminator(BasicBlock &BB) {
  // 只处理 br / brcond
  auto *BI = dyn_cast_or_null<BranchInst>(BB.getTerminator());
  if (!BI)
    return nullptr;
  if (BI->isConditional() && BI->getNumSuccessors() == 2)
    return BI;
  if (BI->isUnconditional() && BI->getNumSuccessors() == 1)
    return BI;
  return nullptr;
}
} // namespace

PreservedAnalyses IndirectBranchPass::run(Function &F,
                                          FunctionAnalysisManager &FAM) {
  errs() << "[vllvm] IndirectBranchPass:" << F.getName() << "\n";
  // indirectbr 会改变 funclet 的结构约束，暂时保留 EH 函数原始实现。
  if (F.hasPersonalityFn()) {
    errs() << "[vllvm] IndirectBranchPass skipped EH function:"
           << F.getName() << "\n";
    return PreservedAnalyses::all();
  }

  LowerSwitchPass LowerSwitch;
  PreservedAnalyses PA = LowerSwitch.run(F, FAM);

  cryptoUtils = std::make_unique<CryptoUtils>(F.getParent());
  if (!getAllBBs(F))
    return PA;

  bool isChanged = makeIndirectBranch(F, makeGloableBBTable(F));
  return isChanged ? PreservedAnalyses::none() : PA;
}
// 获取函数中所有的基本块和分支指令
bool IndirectBranchPass::getAllBBs(Function &F) {
  BBTargets.clear();
  BBNums.clear();
  BrInstSites.clear();

  SmallVector<BasicBlock *, 32> FunctionBlocks;
  for (BasicBlock &BB : F) {
    // LLVM 不允许对入口块取 blockaddress；入口块的 BR 仍参与改写。
    if (&BB != &F.getEntryBlock())
      FunctionBlocks.push_back(&BB);
    // 只处理 br / brcond
    if (BranchInst *BI = getSupportedBranchTerminator(BB))
      BrInstSites.push_back(BI);
  }

  if (BrInstSites.empty())
    return false;

  // 常量表保存全部可寻址块（入口块除外），使用统一的动态索引编号。
  BBTargets.assign(FunctionBlocks.begin(), FunctionBlocks.end());
  for (BasicBlock *BB : BBTargets)
    BBNums[BB] = 0;

  // 打乱基本块顺序
  long seed = cryptoUtils->getRandom32();
  std::default_random_engine e(seed);
  std::shuffle(BBTargets.begin(), BBTargets.end(), e);
  // 生成key映射表
  for (size_t i = 0; i < BBTargets.size(); ++i) {
    BBNums[BBTargets[i]] = static_cast<int>(i);
  }
  return true;
}
GlobalVariable *IndirectBranchPass::makeGloableBBTable(Function &F) {
  if (BBTargets.empty())
    return nullptr;

  std::vector<Constant *> BBArray;
  Module *M = F.getParent();
  LLVMContext &Ctx = F.getContext();
  Type *PtrTy = PointerType::getUnqual(Ctx);
  for (BasicBlock *BB : BBTargets)
    BBArray.push_back(
        ConstantExpr::getBitCast(BlockAddress::get(BB), PtrTy));
  ArrayType *ATy = ArrayType::get(PtrTy, BBArray.size());
  return new GlobalVariable(*M, ATy, true, GlobalValue::PrivateLinkage,
                            ConstantArray::get(ATy, BBArray),
                            "vllvm.ibr.table");
}
bool IndirectBranchPass::makeIndirectBranch(Function &F,
                                            GlobalVariable *BBTableGV) {
  if (BrInstSites.empty() || BBTableGV == nullptr)
    return false;

  LLVMContext &Ctx = F.getContext();
  auto *BBTableTy = cast<ArrayType>(BBTableGV->getValueType());
  Type *I32Ty = Type::getInt32Ty(Ctx);

  // 索引状态位于当前函数栈帧，避免全局变量在线程或递归调用间竞争。
  // volatile 强制保留每个 BR 的读写链，防止优化器还原成固定表项访问。
  IRBuilder<> EntryIRB(&*F.getEntryBlock().getFirstInsertionPt());
  AllocaInst *IndexSlot =
      EntryIRB.CreateAlloca(I32Ty, nullptr, "vllvm.ibr.index");
  StoreInst *InitialStore = EntryIRB.CreateStore(
      ConstantInt::get(I32Ty, cryptoUtils->getRandom32()), IndexSlot);
  InitialStore->setVolatile(true);

  for (BranchInst *BI : BrInstSites) {
    if (!BI->getParent() || BI != BI->getParent()->getTerminator())
      continue;

    SmallVector<BasicBlock *, 4> Destinations;
    auto AddUniqueDestination = [&](BasicBlock *Destination) {
      if (std::find(Destinations.begin(), Destinations.end(), Destination) ==
          Destinations.end())
        Destinations.push_back(Destination);
    };
    for (unsigned I = 0; I != BI->getNumSuccessors(); ++I)
      AddUniqueDestination(BI->getSuccessor(I));
    std::default_random_engine DestinationEngine(cryptoUtils->getRandom32());
    std::shuffle(Destinations.begin(), Destinations.end(), DestinationEngine);

    IRBuilder<> IRB(BI);
    IRB.SetInsertPoint(BI);
    Value *RealIndex = nullptr;
    if (BI->isConditional()) {
      Value *Cond = BI->getCondition();
      Value *TIdx =
          ConstantInt::get(IRB.getInt32Ty(), BBNums[BI->getSuccessor(0)]);
      Value *FIdx =
          ConstantInt::get(IRB.getInt32Ty(), BBNums[BI->getSuccessor(1)]);
      RealIndex = IRB.CreateSelect(Cond, TIdx, FIdx);
    } else {
      RealIndex = ConstantInt::get(I32Ty, BBNums[BI->getSuccessor(0)]);
    }

    LoadInst *OldState =
        IRB.CreateLoad(I32Ty, IndexSlot, "vllvm.ibr.index.old");
    OldState->setVolatile(true);
    RandomizedIntegerCodec IndexCodec(*cryptoUtils);
    Value *EncodedIndex = IndexCodec.encode(
        IRB, RealIndex, OldState, "vllvm.ibr.index.encoded");
    StoreInst *IndexStore = IRB.CreateStore(EncodedIndex, IndexSlot);
    IndexStore->setVolatile(true);
    LoadInst *StoredIndex =
        IRB.CreateLoad(I32Ty, IndexSlot, "vllvm.ibr.index.stored");
    StoredIndex->setVolatile(true);
    Value *Index = IndexCodec.decode(
        IRB, StoredIndex, OldState, "vllvm.ibr.index.decoded");

    // 只通过动态索引读取目标地址，避免额外的 key 加载和地址运算。
    Value *BBPtr = IRB.CreateInBoundsGEP(
        BBTableTy, BBTableGV, {ConstantInt::get(I32Ty, 0), Index});
    Value *BBAddr = IRB.CreateLoad(IRB.getPtrTy(), BBPtr);

    IndirectBrInst *indBr = IRB.CreateIndirectBr(
        BBAddr, static_cast<unsigned>(Destinations.size()));
    for (BasicBlock *Destination : Destinations)
      indBr->addDestination(Destination);
    BI->eraseFromParent();
  }
  return true;
}
