#include "CombinedObfuscationPass.h"

#include "CryptoUtils.h"
#include "Utils.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/Twine.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/Alignment.h"
#include "llvm/Support/RandomNumberGenerator.h"
#include "llvm/Transforms/Utils/LowerSwitch.h"

#include <algorithm>
#include <cstdint>
#include <memory>
#include <optional>
#include <random>
#include <vector>

using namespace llvm;

namespace {
struct SharedConstTable {
  Function &F;
  Module &M;
  const DataLayout &DL;
  LLVMContext &Ctx;
  Type *ConstTy;
  SmallVector<Constant *, 32> Entries;
  GlobalVariable *GV = nullptr;

  SharedConstTable(Function &Fn)
      : F(Fn), M(*Fn.getParent()), DL(M.getDataLayout()), Ctx(Fn.getContext()),
        ConstTy(Type::getInt32Ty(Ctx)) {}

  unsigned size() const { return Entries.size(); }

  unsigned add(uint32_t Value) {
    Entries.push_back(ConstantInt::get(ConstTy, Value));
    return Entries.size() - 1;
  }

  GlobalVariable *createGlobal(const Twine &Name,
                               ArrayRef<Constant *> InitEntries) {
    ArrayType *TableTy = ArrayType::get(ConstTy, InitEntries.size());
    auto *NewGV =
        new GlobalVariable(M, TableTy, false, GlobalValue::PrivateLinkage,
                           ConstantArray::get(TableTy, InitEntries),
                           Name.str());
    NewGV->setAlignment(DL.getABITypeAlign(TableTy));
    return NewGV;
  }

  void ensureMaterialized() {
    if (GV || Entries.empty())
      return;
    GV = createGlobal((Twine("vllvm.combined.const.table.tmp.") + F.getName()),
                      Entries);
  }

  void finalize() {
    if (Entries.empty())
      return;

    GlobalVariable *FinalGV = createGlobal(
        (Twine("vllvm.combined.const.table.") + F.getName()), Entries);
    if (GV) {
      GV->replaceAllUsesWith(FinalGV);
      GV->eraseFromParent();
    }
    GV = FinalGV;
  }

  Value *getEntryPtr(IRBuilder<> &IRB, Value *Index, const Twine &Name) {
    ensureMaterialized();
    return IRB.CreateGEP(ConstTy, GV, Index, Name);
  }

  Value *load(IRBuilder<> &IRB, Value *Index, const Twine &Name) {
    Value *EntryPtr = getEntryPtr(IRB, Index, Name + ".ptr");
    LoadInst *Loaded = IRB.CreateLoad(ConstTy, EntryPtr, Name);
    Loaded->setVolatile(true);
    return Loaded;
  }

  Value *load(IRBuilder<> &IRB, unsigned Index, const Twine &Name) {
    return load(IRB, ConstantInt::get(Type::getInt32Ty(Ctx), Index), Name);
  }
};

struct FlattenPlan {
  bool Enabled = false;
  SmallVector<BasicBlock *, 32> FlattenBBs;
  SmallVector<unsigned, 32> CaseConstIndexes;
  unsigned FirstCaseConstIndex = 0;
  unsigned InitialStateIndex = 0;
};

struct IndirectCallPlan {
  bool Enabled = false;
  std::vector<Function *> Callees;
  DenseMap<Function *, unsigned> CalleeNums;
  std::vector<CallInst *> CallSites;
  DenseMap<CallInst *, uint32_t> CallSiteIndexKeys;
  DenseMap<CallInst *, unsigned> CallSiteConstIndexes;
};

struct LocalSlot {
  AllocaInst *Alloca = nullptr;
  unsigned FieldIndex = 0;
  unsigned ConstIndex = 0;
  uint32_t OffsetKey = 0;
};

struct LocalVarPlan {
  bool Enabled = false;
  StructType *StructTy = nullptr;
  Align MaxAlign = Align(1);
  SmallVector<LocalSlot, 16> Slots;
};

uint32_t makeNonZeroKey(CryptoUtils &Crypto, size_t Index) {
  uint32_t Key = Crypto.getRandom32();
  if (Key != 0)
    return Key;

  Key = Crypto.getRandom32();
  if (Key != 0)
    return Key;

  return 0xA5A5A5A5U ^ static_cast<uint32_t>(Index + 1);
}

uint32_t makeNonZeroKey(RandomNumberGenerator &RNG, unsigned Index) {
  uint32_t Key = static_cast<uint32_t>(RNG());
  if (Key != 0)
    return Key;

  Key = static_cast<uint32_t>(RNG());
  if (Key != 0)
    return Key;

  return 0xA5A5A5A5U ^ static_cast<uint32_t>(Index + 1);
}

Constant *getIntPtrConstant(Type *IntPtrTy, uint64_t Value) {
  return ConstantInt::get(IntPtrTy, Value);
}

unsigned getStateIndexForBB(const FlattenPlan &Plan, BasicBlock *TargetBB) {
  for (unsigned I = 0; I < Plan.FlattenBBs.size(); ++I)
    if (Plan.FlattenBBs[I] == TargetBB)
      return I;
  return Plan.FlattenBBs.size() - 1;
}

bool hasUnsupportedExit(Function &F) {
  for (BasicBlock &BB : F) {
    auto *RI = dyn_cast<ReturnInst>(BB.getTerminator());
    if (!RI)
      continue;

    Instruction *Prev = RI->getPrevNonDebugInstruction();
    auto *CB = dyn_cast_or_null<CallBase>(Prev);
    if (CB && CB->isMustTailCall())
      return true;
  }
  return false;
}

bool hasEH(Function &F) {
  for (BasicBlock &BB : F) {
    for (Instruction &I : BB) {
      if (isa<InvokeInst>(&I) || isa<LandingPadInst>(&I) ||
          isa<CatchPadInst>(&I) || isa<CleanupPadInst>(&I) ||
          isa<CatchSwitchInst>(&I) || isa<CleanupReturnInst>(&I))
        return true;
    }
  }
  return false;
}

bool isSupportedAlloca(AllocaInst &AI, const DataLayout &DL) {
  if (!AI.isStaticAlloca() || AI.isUsedWithInAlloca() || AI.isSwiftError())
    return false;
  if (AI.getAddressSpace() != 0)
    return false;

  Type *AllocatedTy = AI.getAllocatedType();
  if (!AllocatedTy->isSized())
    return false;

  auto *ArraySize = dyn_cast<ConstantInt>(AI.getArraySize());
  if (!ArraySize || ArraySize->isZero() ||
      ArraySize->getValue().getActiveBits() > 63)
    return false;

  std::optional<TypeSize> AllocSize = AI.getAllocationSize(DL);
  if (!AllocSize || AllocSize->isScalable() || AllocSize->isZero())
    return false;

  return true;
}

Type *getFieldType(AllocaInst &AI) {
  auto *ArraySize = cast<ConstantInt>(AI.getArraySize());
  Type *AllocatedTy = AI.getAllocatedType();
  if (ArraySize->isOne())
    return AllocatedTy;
  return ArrayType::get(AllocatedTy, ArraySize->getZExtValue());
}

Instruction *getInsertionPointForUse(Use &U) {
  User *TheUser = U.getUser();
  if (auto *PN = dyn_cast<PHINode>(TheUser)) {
    BasicBlock *IncomingBB = PN->getIncomingBlock(U.getOperandNo());
    return IncomingBB->getTerminator();
  }

  return dyn_cast<Instruction>(TheUser);
}

FlattenPlan planFlatten(Function &F, FunctionAnalysisManager &FAM,
                        CryptoUtils &Crypto, SharedConstTable &ConstTable) {
  FlattenPlan Plan;
  if (F.empty() || F.isDeclaration())
    return Plan;

  LowerSwitchPass LowerSwitch;
  LowerSwitch.run(F, FAM);

  for (BasicBlock &BB : F)
    Plan.FlattenBBs.push_back(&BB);

  if (Plan.FlattenBBs.size() <= 1)
    return Plan;

  for (Function::iterator BB = F.begin(); BB != F.end(); ++BB) {
    if (auto *II = dyn_cast<InvokeInst>(BB->getTerminator())) {
      auto RemoveBB = std::find(Plan.FlattenBBs.begin(),
                                Plan.FlattenBBs.end(), II->getUnwindDest());
      if (RemoveBB != Plan.FlattenBBs.end())
        Plan.FlattenBBs.erase(RemoveBB);
    }
  }

  Plan.FlattenBBs.erase(Plan.FlattenBBs.begin());
  if (Plan.FlattenBBs.empty())
    return Plan;

  BasicBlock *EntryBB = &F.getEntryBlock();
  BranchInst *EntryBr = dyn_cast<BranchInst>(EntryBB->getTerminator());
  if (EntryBB->getTerminator()->getNumSuccessors() > 1 && EntryBr &&
      EntryBr->isConditional()) {
    BasicBlock::iterator I = EntryBB->end();
    --I;
    if (EntryBB->size() > 1)
      --I;

    BasicBlock *TmpBB = EntryBB->splitBasicBlock(I, "first");
    Plan.FlattenBBs.insert(Plan.FlattenBBs.begin(), TmpBB);
  }

  BasicBlock *InitialStateBB = Plan.FlattenBBs.front();
  std::default_random_engine ShuffleEngine(Crypto.getRandom32());
  std::shuffle(Plan.FlattenBBs.begin(), Plan.FlattenBBs.end(), ShuffleEngine);
  Plan.InitialStateIndex = getStateIndexForBB(Plan, InitialStateBB);

  std::vector<uint32_t> CaseValues;
  CaseValues.reserve(Plan.FlattenBBs.size());
  Plan.CaseConstIndexes.reserve(Plan.FlattenBBs.size());
  Plan.FirstCaseConstIndex = ConstTable.size();
  for (unsigned I = 0; I < Plan.FlattenBBs.size(); ++I) {
    uint32_t CaseValue = Crypto.getRandom32BaiscIndex(I);
    while (std::find(CaseValues.begin(), CaseValues.end(), CaseValue) !=
           CaseValues.end()) {
      CaseValue = Crypto.getRandom32();
    }
    CaseValues.push_back(CaseValue);
    Plan.CaseConstIndexes.push_back(ConstTable.add(CaseValue));
  }

  Plan.Enabled = true;
  return Plan;
}

bool applyFlatten(Function &F, FunctionAnalysisManager &FAM,
                  const FlattenPlan &Plan, SharedConstTable &ConstTable) {
  if (!Plan.Enabled)
    return false;

  LLVMContext &Ctx = F.getContext();
  Type *Int32Ty = Type::getInt32Ty(Ctx);
  BasicBlock *EntryBB = &F.getEntryBlock();

  IRBuilder<> IRB(EntryBB);
  EntryBB->getTerminator()->eraseFromParent();
  AllocaInst *SwitchVar = IRB.CreateAlloca(Int32Ty, 0, "switchVar");
  IRB.CreateStore(ConstantInt::get(Int32Ty, Plan.InitialStateIndex),
                  SwitchVar);

  BasicBlock *SwitchLoopEntry =
      BasicBlock::Create(Ctx, "switchLoopEntry", &F, EntryBB);
  BasicBlock *SwitchDispatchEntry =
      BasicBlock::Create(Ctx, "switchDispatchEntry", &F, EntryBB);
  BasicBlock *SwitchLoopEnd =
      BasicBlock::Create(Ctx, "switchLoopEnd", &F, EntryBB);

  EntryBB->moveBefore(SwitchLoopEntry);
  BranchInst::Create(SwitchLoopEntry, EntryBB);
  BranchInst::Create(SwitchLoopEntry, SwitchLoopEnd);
  BasicBlock *SwDefault =
      BasicBlock::Create(Ctx, "switchDefault", &F, SwitchLoopEnd);
  BranchInst::Create(SwitchLoopEnd, SwDefault);

  SmallVector<BasicBlock *, 32> DispatchBBs;
  DispatchBBs.reserve(Plan.FlattenBBs.size());
  for (unsigned I = 0; I < Plan.FlattenBBs.size(); ++I) {
    BasicBlock *DispatchBB =
        BasicBlock::Create(Ctx, "caseDispatch", &F, SwDefault);
    DispatchBBs.push_back(DispatchBB);
  }

  IRB.SetInsertPoint(SwitchLoopEntry);
  Value *StateIndex = IRB.CreateLoad(Int32Ty, SwitchVar, "loadSwitchIndex");
  Value *IsValidIndex = IRB.CreateICmpULT(
      StateIndex, ConstantInt::get(Int32Ty, Plan.FlattenBBs.size()),
      "switchIndexInRange");
  IRB.CreateCondBr(IsValidIndex, SwitchDispatchEntry, SwDefault);

  IRB.SetInsertPoint(SwitchDispatchEntry);
  Value *CaseTableIndex = StateIndex;
  if (Plan.FirstCaseConstIndex != 0) {
    CaseTableIndex = IRB.CreateAdd(
        StateIndex, ConstantInt::get(Int32Ty, Plan.FirstCaseConstIndex),
        "caseTableIndex");
  }
  Value *CaseValue = ConstTable.load(IRB, CaseTableIndex, "loadCaseValue");
  IRB.CreateBr(DispatchBBs.front());

  for (unsigned I = 0; I < DispatchBBs.size(); ++I) {
    IRB.SetInsertPoint(DispatchBBs[I]);
    Value *CaseConst =
        ConstTable.load(IRB, Plan.CaseConstIndexes[I], "loadCaseConst");
    Value *CaseMatch =
        IRB.CreateICmpEQ(CaseValue, CaseConst, "caseConstMatch");
    BasicBlock *NextBB =
        I + 1 < DispatchBBs.size() ? DispatchBBs[I + 1] : SwDefault;
    IRB.CreateCondBr(CaseMatch, Plan.FlattenBBs[I], NextBB);
  }

  for (BasicBlock *BB : Plan.FlattenBBs)
    BB->moveBefore(SwitchLoopEnd);

  for (BasicBlock *BB : Plan.FlattenBBs) {
    Instruction *Term = BB->getTerminator();
    if (Term->getNumSuccessors() == 0)
      continue;
    if (isa<InvokeInst>(Term))
      continue;
    if (Term->getNumSuccessors() > 1 && isa<IndirectBrInst>(Term))
      continue;

    IRBuilder<> BBIRB(Term);
    if (Term->getNumSuccessors() == 1) {
      BasicBlock *SuccBB = Term->getSuccessor(0);
      Value *OldStateIndex =
          BBIRB.CreateLoad(Int32Ty, SwitchVar, "oldStateIndex");
      Value *StateDelta = BBIRB.CreateSub(
          ConstantInt::get(Int32Ty, getStateIndexForBB(Plan, SuccBB)),
          OldStateIndex, "stateDelta");
      Value *NewStateIndex =
          BBIRB.CreateAdd(OldStateIndex, StateDelta, "newStateIndex");
      BBIRB.CreateStore(NewStateIndex, SwitchVar);
      BBIRB.CreateBr(SwitchLoopEntry);
      Term->eraseFromParent();
      continue;
    }

    if (Term->getNumSuccessors() == 2) {
      auto *Br = dyn_cast<BranchInst>(Term);
      if (!Br || !Br->isConditional())
        continue;

      unsigned TrueIndex = getStateIndexForBB(Plan, Term->getSuccessor(0));
      unsigned FalseIndex = getStateIndexForBB(Plan, Term->getSuccessor(1));
      Value *TargetStateIndex =
          BBIRB.CreateSelect(Br->getCondition(),
                             ConstantInt::get(Int32Ty, TrueIndex),
                             ConstantInt::get(Int32Ty, FalseIndex),
                             "targetStateIndex");
      Value *OldStateIndex =
          BBIRB.CreateLoad(Int32Ty, SwitchVar, "oldStateIndex");
      Value *StateDelta =
          BBIRB.CreateSub(TargetStateIndex, OldStateIndex, "stateDelta");
      Value *NewStateIndex =
          BBIRB.CreateAdd(OldStateIndex, StateDelta, "newStateIndex");
      BBIRB.CreateStore(NewStateIndex, SwitchVar);
      BBIRB.CreateBr(SwitchLoopEntry);
      Term->eraseFromParent();
      continue;
    }
  }

  fixStack(&F);
  LowerSwitchPass LowerSwitch;
  LowerSwitch.run(F, FAM);
  return true;
}

IndirectCallPlan planIndirectCalls(Function &F, CryptoUtils &Crypto,
                                   SharedConstTable &ConstTable) {
  IndirectCallPlan Plan;
  if (F.empty() || F.isDeclaration())
    return Plan;

  for (BasicBlock &BB : F) {
    for (Instruction &I : BB) {
      auto *Call = dyn_cast<CallInst>(&I);
      if (!Call)
        continue;

      Function *Callee = Call->getCalledFunction();
      if (!Callee || Callee->isIntrinsic() || Callee->isDeclaration() ||
          Call->isMustTailCall())
        continue;

      Plan.CallSites.push_back(Call);
      if (std::find(Plan.Callees.begin(), Plan.Callees.end(), Callee) ==
          Plan.Callees.end()) {
        Plan.Callees.push_back(Callee);
      }
    }
  }

  if (Plan.CallSites.empty() || Plan.Callees.empty())
    return Plan;

  std::default_random_engine ShuffleEngine(Crypto.getRandom32());
  std::shuffle(Plan.Callees.begin(), Plan.Callees.end(), ShuffleEngine);
  for (unsigned I = 0; I < Plan.Callees.size(); ++I)
    Plan.CalleeNums[Plan.Callees[I]] = I;

  for (unsigned I = 0; I < Plan.CallSites.size(); ++I) {
    CallInst *CallSite = Plan.CallSites[I];
    Function *Callee = CallSite->getCalledFunction();
    uint32_t Key = makeNonZeroKey(Crypto, I);
    uint32_t EncryptedIndex =
        static_cast<uint32_t>(Plan.CalleeNums[Callee]) ^ Key;
    Plan.CallSiteIndexKeys[CallSite] = Key;
    Plan.CallSiteConstIndexes[CallSite] = ConstTable.add(EncryptedIndex);
  }

  Plan.Enabled = true;
  return Plan;
}

GlobalVariable *createFuncTable(Function &F, const IndirectCallPlan &Plan) {
  if (!Plan.Enabled)
    return nullptr;

  Module *M = F.getParent();
  LLVMContext &Ctx = M->getContext();
  Type *VoidPtrTy = PointerType::getUnqual(Ctx);
  ArrayType *FuncTableTy = ArrayType::get(VoidPtrTy, Plan.Callees.size());
  std::vector<Constant *> FuncPtrs;
  FuncPtrs.reserve(Plan.Callees.size());

  for (Function *Callee : Plan.Callees)
    FuncPtrs.push_back(ConstantExpr::getBitCast(Callee, VoidPtrTy));

  return new GlobalVariable(*M, FuncTableTy, true,
                            GlobalValue::PrivateLinkage,
                            ConstantArray::get(FuncTableTy, FuncPtrs),
                            (Twine("func_table") + F.getName()).str());
}

bool applyIndirectCalls(Function &F, const IndirectCallPlan &Plan,
                        SharedConstTable &ConstTable) {
  if (!Plan.Enabled)
    return false;

  GlobalVariable *FuncTableGV = createFuncTable(F, Plan);
  if (!FuncTableGV)
    return false;

  LLVMContext &Ctx = F.getContext();
  Type *Int32Ty = Type::getInt32Ty(Ctx);
  auto *FuncTableTy = cast<ArrayType>(FuncTableGV->getValueType());

  for (CallInst *Call : Plan.CallSites) {
    IRBuilder<> IRB(Call);
    Value *EncryptedIndex = ConstTable.load(
        IRB, Plan.CallSiteConstIndexes.lookup(Call), "vllvm.icall.enc_index");
    Value *Index = IRB.CreateXor(
        EncryptedIndex,
        ConstantInt::get(Int32Ty, Plan.CallSiteIndexKeys.lookup(Call)),
        "vllvm.icall.index");
    Value *FuncPtr = IRB.CreateInBoundsGEP(
        FuncTableTy, FuncTableGV,
        {ConstantInt::get(Type::getInt32Ty(Ctx), 0), Index});
    Value *FuncAddr = IRB.CreateLoad(IRB.getPtrTy(), FuncPtr,
                                     "vllvm.icall.func");
    Call->setCalledOperand(FuncAddr);
  }

  return true;
}

LocalVarPlan planLocalVars(Function &F, SharedConstTable &ConstTable) {
  LocalVarPlan Plan;
  if (F.empty() || F.isDeclaration() || F.hasFnAttribute(Attribute::Naked) ||
      hasUnsupportedExit(F) || hasEH(F))
    return Plan;

  Module *M = F.getParent();
  const DataLayout &DL = M->getDataLayout();
  LLVMContext &Ctx = F.getContext();

  SmallVector<AllocaInst *, 16> Allocas;
  for (Instruction &I : F.getEntryBlock()) {
    auto *AI = dyn_cast<AllocaInst>(&I);
    if (AI && isSupportedAlloca(*AI, DL))
      Allocas.push_back(AI);
  }
  if (Allocas.empty())
    return Plan;

  for (AllocaInst *AI : Allocas)
    for (Use &U : AI->uses())
      if (!getInsertionPointForUse(U))
        return Plan;

  SmallVector<Type *, 32> FieldTypes;
  uint64_t CurrentOffset = 0;

  for (AllocaInst *AI : Allocas) {
    Type *FieldTy = getFieldType(*AI);
    TypeSize FieldSize = DL.getTypeAllocSize(FieldTy);
    if (FieldSize.isScalable())
      return LocalVarPlan();

    Align FieldAlign = std::max(AI->getAlign(), DL.getABITypeAlign(FieldTy));
    Plan.MaxAlign = std::max(Plan.MaxAlign, FieldAlign);

    uint64_t AlignedOffset = alignTo(CurrentOffset, FieldAlign);
    if (AlignedOffset != CurrentOffset) {
      FieldTypes.push_back(
          ArrayType::get(Type::getInt8Ty(Ctx), AlignedOffset - CurrentOffset));
      CurrentOffset = AlignedOffset;
    }

    LocalSlot Slot;
    Slot.Alloca = AI;
    Slot.FieldIndex = FieldTypes.size();
    Plan.Slots.push_back(Slot);

    FieldTypes.push_back(FieldTy);
    CurrentOffset += FieldSize.getFixedValue();
  }

  if (FieldTypes.empty())
    return LocalVarPlan();

  Plan.StructTy = StructType::create(
      Ctx, FieldTypes, (Twine("vllvm.localvars.") + F.getName()).str());
  const StructLayout *Layout = DL.getStructLayout(Plan.StructTy);
  for (const LocalSlot &Slot : Plan.Slots)
    if (Layout->getElementOffset(Slot.FieldIndex) > UINT32_MAX)
      return LocalVarPlan();

  std::unique_ptr<RandomNumberGenerator> RNG =
      M->createRNG((Twine("vllvm.combined.localvars.") + F.getName()).str());
  for (unsigned SlotNo = 0; SlotNo < Plan.Slots.size(); ++SlotNo) {
    LocalSlot &Slot = Plan.Slots[SlotNo];
    uint32_t OffsetKey = makeNonZeroKey(*RNG, SlotNo);
    uint32_t EncryptedOffset =
        static_cast<uint32_t>(Layout->getElementOffset(Slot.FieldIndex)) ^
        OffsetKey;

    Slot.OffsetKey = OffsetKey;
    Slot.ConstIndex = ConstTable.add(EncryptedOffset);
  }

  Plan.Enabled = true;
  return Plan;
}

bool applyLocalVars(Function &F, const LocalVarPlan &Plan,
                    SharedConstTable &ConstTable) {
  if (!Plan.Enabled)
    return false;

  Module *M = F.getParent();
  const DataLayout &DL = M->getDataLayout();
  LLVMContext &Ctx = F.getContext();
  Type *IntPtrTy = DL.getIntPtrType(Ctx);
  unsigned PtrBits = cast<IntegerType>(IntPtrTy)->getBitWidth();

  Instruction *InsertBefore = &*F.getEntryBlock().getFirstInsertionPt();
  IRBuilder<> FirstIRB(InsertBefore);

  Constant *StructSize = ConstantExpr::getSizeOf(Plan.StructTy);
  StructSize = ConstantExpr::getTruncOrBitCast(StructSize, IntPtrTy);

  uint64_t ExtraAlignBytes = Plan.MaxAlign.value() - 1;
  Constant *AllocSize = ConstantExpr::getAdd(
      StructSize, getIntPtrConstant(IntPtrTy, ExtraAlignBytes));

  CallInst *RawStructPtr = FirstIRB.CreateMalloc(
      IntPtrTy, Plan.StructTy, AllocSize, nullptr, nullptr,
      "vllvm.locals.raw");
  Value *StructPtr = RawStructPtr;
  if (Plan.MaxAlign.value() > 1) {
    Value *RawInt =
        FirstIRB.CreatePtrToInt(RawStructPtr, IntPtrTy, "vllvm.locals.int");
    Value *Biased = FirstIRB.CreateAdd(
        RawInt, getIntPtrConstant(IntPtrTy, ExtraAlignBytes),
        "vllvm.locals.bias");
    Constant *Mask =
        ConstantInt::get(IntPtrTy,
                         APInt(PtrBits, ~(Plan.MaxAlign.value() - 1)));
    Value *AlignedInt =
        FirstIRB.CreateAnd(Biased, Mask, "vllvm.locals.aligned_int");
    StructPtr =
        FirstIRB.CreateIntToPtr(AlignedInt, FirstIRB.getPtrTy(),
                                "vllvm.locals");
  }

  for (unsigned SlotNo = 0; SlotNo < Plan.Slots.size(); ++SlotNo) {
    const LocalSlot &Slot = Plan.Slots[SlotNo];
    std::string SlotName = Slot.Alloca->hasName()
                               ? (Slot.Alloca->getName() + ".slot").str()
                               : "vllvm.local.slot";

    SmallVector<Use *, 16> Uses;
    for (Use &U : Slot.Alloca->uses())
      Uses.push_back(&U);

    for (Use *U : Uses) {
      Instruction *InsertPt = getInsertionPointForUse(*U);
      IRBuilder<> UseIRB(InsertPt);
      Value *EncryptedOffset = ConstTable.load(UseIRB, Slot.ConstIndex,
                                               "vllvm.local.enc_offset");
      Value *Offset32 =
          UseIRB.CreateXor(EncryptedOffset,
                           ConstantInt::get(Type::getInt32Ty(Ctx),
                                            Slot.OffsetKey),
                           "vllvm.local.offset32");
      Value *Offset =
          UseIRB.CreateZExtOrBitCast(Offset32, IntPtrTy, "vllvm.local.offset");
      Value *SlotPtr =
          UseIRB.CreateGEP(FirstIRB.getInt8Ty(), StructPtr, Offset, SlotName);
      U->set(SlotPtr);
    }
  }

  for (const LocalSlot &Slot : Plan.Slots)
    Slot.Alloca->eraseFromParent();

  SmallVector<ReturnInst *, 8> Returns;
  for (BasicBlock &BB : F)
    if (auto *RI = dyn_cast<ReturnInst>(BB.getTerminator()))
      Returns.push_back(RI);

  for (ReturnInst *RI : Returns) {
    IRBuilder<> FreeIRB(RI);
    FreeIRB.CreateFree(RawStructPtr);
  }

  return true;
}
} // namespace

PreservedAnalyses CombinedObfuscationPass::run(Function &F,
                                               FunctionAnalysisManager &FAM) {
  errs() << "[vllvm] CombinedObfuscationPass:" << F.getName() << "\n";
  bool Changed = runCombined(F, FAM);
  return Changed ? PreservedAnalyses::none() : PreservedAnalyses::all();
}

bool CombinedObfuscationPass::runCombined(Function &F,
                                          FunctionAnalysisManager &FAM) {
  if (F.empty() || F.isDeclaration())
    return false;

  SharedConstTable ConstTable(F);
  CryptoUtils Crypto(F.getParent());
  bool Changed = false;

  FlattenPlan FPlan = planFlatten(F, FAM, Crypto, ConstTable);
  if (FPlan.Enabled) {
    ConstTable.ensureMaterialized();
    Changed |= applyFlatten(F, FAM, FPlan, ConstTable);
  }

  IndirectCallPlan IPlan = planIndirectCalls(F, Crypto, ConstTable);
  if (IPlan.Enabled) {
    ConstTable.ensureMaterialized();
    Changed |= applyIndirectCalls(F, IPlan, ConstTable);
  }

  LocalVarPlan LPlan = planLocalVars(F, ConstTable);
  if (LPlan.Enabled) {
    ConstTable.ensureMaterialized();
    Changed |= applyLocalVars(F, LPlan, ConstTable);
  }

  if (Changed)
    ConstTable.finalize();

  return Changed;
}
