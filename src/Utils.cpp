#include "Utils.h"

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
    : SelectedAlgorithm(
          static_cast<Algorithm>(Crypto.getRandom32() % 3U)) {}

Value *RandomizedIntegerCodec::encode(IRBuilder<> &IRB, Value *Input,
                                      Value *Key, const Twine &Name) const {
  switch (SelectedAlgorithm) {
  case Algorithm::Xor:
    return IRB.CreateXor(Input, Key, Name);
  case Algorithm::Add:
    return IRB.CreateAdd(Input, Key, Name);
  case Algorithm::Sub:
    return IRB.CreateSub(Input, Key, Name);
  }
  llvm_unreachable("unknown reversible integer encoding algorithm");
}

Value *RandomizedIntegerCodec::decode(IRBuilder<> &IRB, Value *Input,
                                      Value *Key, const Twine &Name) const {
  switch (SelectedAlgorithm) {
  case Algorithm::Xor:
    return IRB.CreateXor(Input, Key, Name);
  case Algorithm::Add:
    return IRB.CreateSub(Input, Key, Name);
  case Algorithm::Sub:
    return IRB.CreateAdd(Input, Key, Name);
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

void fixStack(Function *f) {
  // Try to remove phi node and demote reg to stack
  std::vector<PHINode *> tmpPhi;
  std::vector<Instruction *> tmpReg;
  BasicBlock *bbEntry = &*f->begin();

  do {
    tmpPhi.clear();
    tmpReg.clear();

    for (Function::iterator i = f->begin(); i != f->end(); ++i) {
      for (BasicBlock::iterator j = i->begin(); j != i->end(); ++j) {
        if (isa<PHINode>(j)) {
          PHINode *phi = cast<PHINode>(j);
          tmpPhi.push_back(phi);
          continue;
        }
        if (!(isa<AllocaInst>(j) && j->getParent() == bbEntry) &&
            (valueEscapes(&*j) || j->isUsedOutsideOfBlock(&*i))) {
          tmpReg.push_back(&*j);
          continue;
        }
      }
    }
    for (unsigned int i = 0; i != tmpReg.size(); ++i) {
      DemoteRegToStack(*tmpReg.at(i));
    }

    for (unsigned int i = 0; i != tmpPhi.size(); ++i) {
      DemotePHIToStack(tmpPhi.at(i));
    }

  } while (tmpReg.size() != 0 || tmpPhi.size() != 0);
}

void fixStackForFlatten(Function *F) {
  if (!hasEH(*F)) {
    fixStack(F);
    return;
  }

  // C++ EH 中 invoke/landingpad 有特殊定义域，不能套用全函数反复
  // Demote 的旧 fixStack；这里只处理 flatten 必需的 SSA 跨块值。
  splitInvokeNormalEdgesForPHI(*F);

  SmallVector<Instruction *, 32> Regs;
  SmallVector<PHINode *, 16> Phis;
  BasicBlock *EntryBB = &F->getEntryBlock();

  for (BasicBlock &BB : *F) {
    for (Instruction &I : BB) {
      if (auto *PN = dyn_cast<PHINode>(&I)) {
        Phis.push_back(PN);
        continue;
      }

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

  for (PHINode *PN : Phis)
    if (PN->getParent())
      DemotePHIToStack(PN);
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
