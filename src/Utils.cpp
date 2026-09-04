#include "Utils.h"

#include "llvm/IR/DebugInfoMetadata.h"
#include "llvm/IR/DiagnosticInfo.h"
#include "llvm/IR/EHPersonalities.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/NoFolder.h"
#include "llvm/ADT/SmallVector.h"

namespace {
bool hasEH(Function &F) {
  for (Instruction &I : instructions(F)) {
    if (isa<InvokeInst>(&I) || isa<LandingPadInst>(&I) ||
        isa<CatchPadInst>(&I) || isa<CleanupPadInst>(&I) ||
        isa<CatchSwitchInst>(&I) || isa<CatchReturnInst>(&I) ||
        isa<CleanupReturnInst>(&I) || isa<ResumeInst>(&I))
      return true;
  }
  return false;
}

bool shouldSkipEHValue(Instruction &I) {
  return I.isEHPad() || isa<LandingPadInst>(&I) || isa<CatchPadInst>(&I) ||
         isa<CleanupPadInst>(&I) || isa<CatchSwitchInst>(&I);
}

void updatePhiIncomingBlock(BasicBlock *SuccBB, BasicBlock *OldPred,
                            BasicBlock *NewPred) {
  for (PHINode &PN : SuccBB->phis()) {
    for (unsigned I = 0, E = PN.getNumIncomingValues(); I != E; ++I)
      if (PN.getIncomingBlock(I) == OldPred)
        PN.setIncomingBlock(I, NewPred);
  }
}

void splitInvokeNormalEdgesForPHI(Function &F) {
  SmallVector<InvokeInst *, 8> Invokes;
  for (Instruction &I : instructions(F))
    if (auto *II = dyn_cast<InvokeInst>(&I))
      Invokes.push_back(II);

  for (InvokeInst *II : Invokes) {
    BasicBlock *NormalDest = II->getNormalDest();
    BasicBlock *InvokeBB = II->getParent();
    bool NeedsSplit = false;
    for (PHINode &PN : NormalDest->phis()) {
      for (unsigned I = 0, E = PN.getNumIncomingValues(); I != E; ++I) {
        if (PN.getIncomingBlock(I) == InvokeBB) {
          NeedsSplit = true;
          break;
        }
      }
      if (NeedsSplit)
        break;
    }
    if (!NeedsSplit)
      continue;

    // invoke 的返回值只能在 normal 边之后使用；把 PHI incoming 改到
    // 专用跳板块，后续 DemotePHIToStack 才能把 store 插在合法位置。
    BasicBlock *SplitBB = BasicBlock::Create(
        F.getContext(), "invoke.phi.edge", &F, NormalDest);
    BranchInst::Create(NormalDest, SplitBB);
    updatePhiIncomingBlock(NormalDest, InvokeBB, SplitBB);
    II->setNormalDest(SplitBB);
  }
}
} // namespace

RandomizedIntegerCodec::RandomizedIntegerCodec(CryptoUtils &Crypto)
    : SelectedMode(static_cast<Mode>(Crypto.getRandom32() % 5U)),
      SelectedAlgorithm(
          static_cast<Algorithm>(Crypto.getRandom32() % 3U)),
      KeyMultiplier(Crypto.getRandom64() | 1ULL),
      KeyXor(Crypto.getRandom64()) {
  if (SelectedMode == Mode::Add)
    SelectedAlgorithm = Algorithm::Add;
  else if (SelectedMode == Mode::Xor)
    SelectedAlgorithm = Algorithm::Xor;
  else if (SelectedMode == Mode::Sub)
    SelectedAlgorithm = Algorithm::Sub;
}

Value *RandomizedIntegerCodec::mixKey(IRBuilder<> &IRB, Value *Key,
                                      const Twine &Name) const {
  auto *KeyTy = cast<IntegerType>(Key->getType());
  Value *Mixed = IRB.CreateMul(
      Key, ConstantInt::get(KeyTy, KeyMultiplier), Name + ".mul");
  return IRB.CreateXor(Mixed, ConstantInt::get(KeyTy, KeyXor),
                       Name + ".xor");
}

Value *RandomizedIntegerCodec::encode(IRBuilder<> &IRB, Value *Input,
                                      Value *Key, const Twine &Name) const {
  if (SelectedMode == Mode::None)
    return Input;
  Value *EffectiveKey = SelectedMode == Mode::Mixed
                            ? mixKey(IRB, Key, Name + ".key")
                            : Key;
  switch (SelectedAlgorithm) {
  case Algorithm::Xor:
    return IRB.CreateXor(Input, EffectiveKey, Name);
  case Algorithm::Add:
    return IRB.CreateAdd(Input, EffectiveKey, Name);
  case Algorithm::Sub:
    return IRB.CreateSub(Input, EffectiveKey, Name);
  }
  llvm_unreachable("unknown reversible integer encoding algorithm");
}

Value *RandomizedIntegerCodec::decode(IRBuilder<> &IRB, Value *Input,
                                      Value *Key, const Twine &Name) const {
  if (SelectedMode == Mode::None)
    return Input;
  Value *EffectiveKey = SelectedMode == Mode::Mixed
                            ? mixKey(IRB, Key, Name + ".key")
                            : Key;
  switch (SelectedAlgorithm) {
  case Algorithm::Xor:
    return IRB.CreateXor(Input, EffectiveKey, Name);
  case Algorithm::Add:
    return IRB.CreateSub(Input, EffectiveKey, Name);
  case Algorithm::Sub:
    return IRB.CreateAdd(Input, EffectiveKey, Name);
  }
  llvm_unreachable("unknown reversible integer encoding algorithm");
}

// Shamefully borrowed from ../Scalar/RegToMem.cpp :(
bool valueEscapes(Instruction *Inst) {
  BasicBlock *BB = Inst->getParent();
  for (Value::use_iterator UI = Inst->use_begin(), E = Inst->use_end(); UI != E;
       ++UI) {
    Instruction *I = cast<Instruction>(*UI);
    if (I->getParent() != BB || isa<PHINode>(I)) {
      return true;
    }
  }
  return false;
}

PHILoweringResult lowerPHINodes(Function &F) {
  SmallVector<PHINode *, 16> Phis;
  for (BasicBlock &BB : F) {
    for (PHINode &PN : BB.phis()) {
      // catchswitch 没有普通指令插入点；callbr 结果不能在其定义前 store。
      bool Supported = PN.getType()->isSized() &&
                       !isa<CatchSwitchInst>(BB.getTerminator());
      for (unsigned I = 0; I < PN.getNumIncomingValues(); ++I) {
        BasicBlock *Pred = PN.getIncomingBlock(I);
        auto *CallBr = dyn_cast<CallBrInst>(PN.getIncomingValue(I));
        Supported &= !isa<CatchSwitchInst>(Pred->getTerminator()) &&
                     !(CallBr && CallBr->getParent() == Pred);
      }
      if (!Supported) {
        errs() << "[vllvm] PHI lowering skipped unsupported edge/type in:"
               << F.getName() << "\n";
        return PHILoweringResult::Unsupported;
      }
      Phis.push_back(&PN);
    }
  }
  if (Phis.empty())
    return PHILoweringResult::Unchanged;

  // 先完整检查再修改；invoke 返回值必须在 normal 边的跳板中写入栈槽。
  splitInvokeNormalEdgesForPHI(F);
  for (PHINode *PN : Phis) {
    DebugLoc Loc = PN->getDebugLoc();
    if (!Loc && F.getSubprogram())
      Loc = DILocation::get(F.getContext(), 0, 0, F.getSubprogram());
    AllocaInst *Slot = DemotePHIToStack(PN);
    if (!Slot)
      continue;
    // 后续 enstr 会在 store 前插入调用，必须保留合法的调试位置。
    Slot->setDebugLoc(Loc);
    for (User *U : Slot->users())
      if (auto *I = dyn_cast<Instruction>(U))
        I->setDebugLoc(Loc);
  }
  return PHILoweringResult::Lowered;
}

void fixStack(Function *F) {
  if (F->empty())
    return;
  BasicBlock *Entry = &F->getEntryBlock();
  while (true) {
    if (lowerPHINodes(*F) == PHILoweringResult::Unsupported)
      return;
    SmallVector<Instruction *, 16> Regs;
    for (Instruction &I : instructions(F)) {
      if (!(isa<AllocaInst>(I) && I.getParent() == Entry) &&
          (valueEscapes(&I) || I.isUsedOutsideOfBlock(I.getParent())))
        Regs.push_back(&I);
    }
    if (Regs.empty())
      return;
    for (Instruction *I : Regs)
      DemoteRegToStack(*I);
  }
}

void fixStackForFlatten(Function *F) {
  if (lowerPHINodes(*F) == PHILoweringResult::Unsupported)
    return;
  if (!hasEH(*F)) {
    fixStack(F);
    return;
  }

  // C++ EH 中 invoke/landingpad 有特殊定义域，不能套用全函数反复
  // Demote 的旧 fixStack；这里只处理 flatten 必需的 SSA 跨块值。
  SmallVector<Instruction *, 32> Regs;
  BasicBlock *EntryBB = &F->getEntryBlock();

  for (BasicBlock &BB : *F) {
    for (Instruction &I : BB) {
      if (I.getType()->isVoidTy() || shouldSkipEHValue(I))
        continue;
      if (isa<AllocaInst>(&I) && I.getParent() == EntryBB)
        continue;
      if (I.isTerminator() && !isa<InvokeInst>(&I) && !isa<CallBrInst>(&I))
        continue;
      if (valueEscapes(&I) || I.isUsedOutsideOfBlock(&BB))
        Regs.push_back(&I);
    }
  }

  for (Instruction *I : Regs)
    if (I->getParent())
      DemoteRegToStack(*I);

  lowerPHINodes(*F);
}

CallBase *fixEH(CallBase *CB) {
  const auto BB = CB->getParent();
  if (!BB) {
    return CB;
  }
  const auto Fn = BB->getParent();
  if (!Fn || !Fn->hasPersonalityFn() ||
      !isScopedEHPersonality(classifyEHPersonality(Fn->getPersonalityFn()))) {
    return CB;
  }
  const auto BlockColors = colorEHFunclets(*Fn);
  const auto BBColor = BlockColors.find(BB);
  if (BBColor == BlockColors.end()) {
    return CB;
  }
  const auto &ColorVec = BBColor->getSecond();
  assert(ColorVec.size() == 1 && "non-unique color for block!");

  const auto EHBlock = ColorVec.front();
  if (!EHBlock || !EHBlock->isEHPad()) {
    return CB;
  }
  const auto EHPad = EHBlock->getFirstNonPHI();

  const OperandBundleDef OB("funclet", EHPad);
  auto *NewCall =
      CallBase::addOperandBundle(CB, LLVMContext::OB_funclet, OB, CB);
  NewCall->copyMetadata(*CB);
  CB->replaceAllUsesWith(NewCall);
  CB->eraseFromParent();
  return NewCall;
}

void insertFreeOnFunctionExits(Function &F, Value *Ptr) {
  SmallVector<Instruction *, 8> ExitTerms;
  for (BasicBlock &BB : F) {
    Instruction *Term = BB.getTerminator();
    if (isa<ReturnInst>(Term) || isa<ResumeInst>(Term)) {
      ExitTerms.push_back(Term);
      continue;
    }

    auto *CRI = dyn_cast<CleanupReturnInst>(Term);
    if (CRI && CRI->unwindsToCaller())
      ExitTerms.push_back(Term);
  }

  for (Instruction *Term : ExitTerms) {
    IRBuilder<> FreeIRB(Term);
    CallInst *FreeCall = FreeIRB.CreateFree(Ptr);
    fixEH(FreeCall);
  }
}

void LowerConstantExpr(Function &F) {
  SmallPtrSet<Instruction *, 8> WorkList;

  for (inst_iterator It = inst_begin(F), E = inst_end(F); It != E; ++It) {
    Instruction *I = &*It;

    if (isa<LandingPadInst>(I) || isa<CatchPadInst>(I) ||
        isa<CatchSwitchInst>(I) || isa<CatchReturnInst>(I))
      continue;
    if (auto *II = dyn_cast<IntrinsicInst>(I)) {
      if (II->getIntrinsicID() == Intrinsic::eh_typeid_for) {
        continue;
      }
    }

    for (unsigned int i = 0; i < I->getNumOperands(); ++i) {
      if (isa<ConstantExpr>(I->getOperand(i)))
        WorkList.insert(I);
    }
  }

  while (!WorkList.empty()) {
    auto It = WorkList.begin();
    Instruction *I = *It;
    WorkList.erase(*It);

    if (PHINode *PHI = dyn_cast<PHINode>(I)) {
      for (unsigned int i = 0; i < PHI->getNumIncomingValues(); ++i) {
        Instruction *TI = PHI->getIncomingBlock(i)->getTerminator();
        if (ConstantExpr *CE =
                dyn_cast<ConstantExpr>(PHI->getIncomingValue(i))) {
          Instruction *NewInst = CE->getAsInstruction();
          NewInst->insertBefore(TI);
          PHI->setIncomingValue(i, NewInst);
          WorkList.insert(NewInst);
        }
      }
    } else {
      for (unsigned int i = 0; i < I->getNumOperands(); ++i) {
        if (ConstantExpr *CE = dyn_cast<ConstantExpr>(I->getOperand(i))) {
          Instruction *NewInst = CE->getAsInstruction();
          NewInst->insertBefore(I);
          I->replaceUsesOfWith(CE, NewInst);
          WorkList.insert(NewInst);
        }
      }
    }
  }
}

bool expandConstantExpr(Function &F) {
  bool Changed = false;
  LLVMContext &Ctx = F.getContext();
  IRBuilder<NoFolder> IRB(Ctx);

  for (auto &BB : F) {
    for (auto &I : BB) {
      if (I.isEHPad() || isa<AllocaInst>(&I) || isa<IntrinsicInst>(&I) ||
          isa<SwitchInst>(&I) || I.isAtomic()) {
        continue;
      }
      auto CI = dyn_cast<CallInst>(&I);
      auto GEP = dyn_cast<GetElementPtrInst>(&I);
      auto IsPhi = isa<PHINode>(&I);
      auto InsertPt =
          IsPhi ? F.getEntryBlock().getFirstInsertionPt() : I.getIterator();
      for (unsigned i = 0; i < I.getNumOperands(); ++i) {
        if (CI && CI->isBundleOperand(i)) {
          continue;
        }
        if (GEP && (i < 2 || GEP->getSourceElementType()->isStructTy())) {
          continue;
        }
        auto Opr = I.getOperand(i);
        if (auto CEP = dyn_cast<ConstantExpr>(Opr)) {
          IRB.SetInsertPoint(InsertPt);
          auto CEPInst = CEP->getAsInstruction();
          IRB.Insert(CEPInst);
          I.setOperand(i, CEPInst);
          Changed = true;
        }
      }
    }
  }
  return Changed;
}
