#include "IndirectBranchPass.h"
#include "Utils.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/IR/Constant.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/IRBuilder.h"

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
  // 任意增加 CFG 前驱会破坏 funclet 的结构约束，暂时保留 EH 函数
  // 原始实现。
  if (F.hasPersonalityFn()) {
    errs() << "[vllvm] IndirectBranchPass skipped EH function:"
           << F.getName() << "\n";
    return PreservedAnalyses::all();
  }

  cryptoUtils = std::make_unique<CryptoUtils>(F.getParent());
  if (!getAllBBs(F))
    return PreservedAnalyses::all();

  // 假目标会成为真实 CFG 前驱；先移除 PHI 和跨块 SSA，保证新增边后
  // IR 合法。再次收集以覆盖 Demote 可能新建的块和边。
  fixStackForFlatten(&F);
  if (!getAllBBs(F))
    return PreservedAnalyses::none();
  bool isChanged = makeIndirectBranch(F, makeGloableBBTable(F));
  return isChanged ? PreservedAnalyses::none() : PreservedAnalyses::all();
}
// 获取函数中所有的基本块和分支指令
bool IndirectBranchPass::getAllBBs(Function &F) {
  BBTargets.clear();
  BBNums.clear();
  BrInstSites.clear();
  FakeBBTargets.clear();

  SmallVector<BasicBlock *, 32> FunctionBlocks;
  for (BasicBlock &BB : F) {
    FunctionBlocks.push_back(&BB);
    // 只处理 br / brcond
    if (BranchInst *BI = getSupportedBranchTerminator(BB))
      BrInstSites.push_back(BI);
  }

  if (BrInstSites.empty())
    return false;

  // 常量表保存当前函数的全部块地址；即使某个块本轮没有被选作假目标，
  // 动态索引仍与完整函数 CFG 使用同一编号空间。
  BBTargets.assign(FunctionBlocks.begin(), FunctionBlocks.end());
  for (BasicBlock *BB : BBTargets)
    BBNums[BB] = 0;

  // 每个原始 br 独立选择一到两个已有块作为假目标。入口块、当前块和
  // 真实 successor 不能作为假目标，否则会形成非法入口前驱或重复目的地。
  for (BranchInst *BI : BrInstSites) {
    SmallVector<BasicBlock *, 32> Candidates;
    for (BasicBlock *Candidate : FunctionBlocks) {
      if (Candidate == &F.getEntryBlock() || Candidate == BI->getParent() ||
          Candidate->isEHPad())
        continue;

      bool IsRealSuccessor = false;
      for (unsigned I = 0; I != BI->getNumSuccessors(); ++I)
        IsRealSuccessor |= Candidate == BI->getSuccessor(I);
      if (!IsRealSuccessor)
        Candidates.push_back(Candidate);
    }

    // 极小 CFG 没有额外目的块时整函数不做转换，避免产生单目标
    // indirectbr。
    if (Candidates.empty()) {
      errs() << "[vllvm] IndirectBranchPass has no fake target:"
             << F.getName() << "\n";
      BBTargets.clear();
      BBNums.clear();
      BrInstSites.clear();
      FakeBBTargets.clear();
      return false;
    }

    std::default_random_engine CandidateEngine(cryptoUtils->getRandom32());
    std::shuffle(Candidates.begin(), Candidates.end(), CandidateEngine);
    const unsigned RequestedCount = 1U + (cryptoUtils->getRandom32() & 1U);
    const unsigned FakeCount = std::min<unsigned>(
        RequestedCount, static_cast<unsigned>(Candidates.size()));
    auto &Targets = FakeBBTargets[BI];
    for (unsigned I = 0; I != FakeCount; ++I) {
      BasicBlock *Target = Candidates[I];
      Targets.push_back(Target);
    }
  }

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
    for (BasicBlock *FakeTarget : FakeBBTargets[BI])
      AddUniqueDestination(FakeTarget);
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
    Value *SelectedIndex = RealIndex;
    Value *IndexMask = OldState;
    // 连续整数乘积最低位恒为 0；volatile 状态让假索引保留为显式候选，
    // 但运行时仍只会选择真实 successor。
    for (BasicBlock *FakeTarget : FakeBBTargets[BI]) {
      Constant *FakeIndex = ConstantInt::get(I32Ty, BBNums[FakeTarget]);
      Constant *FakeMask = ConstantInt::get(I32Ty, BBNums[FakeTarget] + 1);
      Value *OpaqueInput = IRB.CreateXor(OldState, FakeMask);
      Value *Adjacent =
          IRB.CreateAdd(OpaqueInput, ConstantInt::get(I32Ty, 1));
      Value *EvenProduct = IRB.CreateMul(OpaqueInput, Adjacent);
      Value *LowBit =
          IRB.CreateAnd(EvenProduct, ConstantInt::get(I32Ty, 1));
      Value *ChooseFake = IRB.CreateICmpNE(
          LowBit, ConstantInt::get(I32Ty, 0));
      SelectedIndex = IRB.CreateSelect(ChooseFake, FakeIndex, SelectedIndex);
      IndexMask = IRB.CreateXor(
          IndexMask, FakeMask, "vllvm.ibr.index.fake");
    }
    RandomizedIntegerCodec IndexCodec(*cryptoUtils);
    Value *EncodedIndex = IndexCodec.encode(
        IRB, SelectedIndex, IndexMask, "vllvm.ibr.index.encoded");
    StoreInst *IndexStore = IRB.CreateStore(EncodedIndex, IndexSlot);
    IndexStore->setVolatile(true);
    LoadInst *StoredIndex =
        IRB.CreateLoad(I32Ty, IndexSlot, "vllvm.ibr.index.stored");
    StoredIndex->setVolatile(true);
    Value *Index = IndexCodec.decode(
        IRB, StoredIndex, IndexMask, "vllvm.ibr.index.decoded");

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
