#include "LocalVarStructPass.h"

#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/Twine.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/Alignment.h"

#include <algorithm>
#include <cstdint>
#include <optional>

using namespace llvm;

namespace {
struct LocalSlot {
  AllocaInst *Alloca = nullptr;
  Type *FieldTy = nullptr;
  unsigned FieldIndex = 0;
  bool IsArrayAllocation = false;
};

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

Constant *getIntPtrConstant(Type *IntPtrTy, uint64_t Value) {
  return ConstantInt::get(IntPtrTy, Value);
}
} // namespace

PreservedAnalyses LocalVarStructPass::run(Function &F,
                                          FunctionAnalysisManager &FAM) {
  errs() << "[vllvm] LocalVarStructPass:" << F.getName() << "\n";
  bool IsChanged = moveAllocasToStruct(F);
  return IsChanged ? PreservedAnalyses::none() : PreservedAnalyses::all();
}

bool LocalVarStructPass::moveAllocasToStruct(Function &F) {
  if (F.empty() || F.isDeclaration() || F.hasFnAttribute(Attribute::Naked) ||
      hasUnsupportedExit(F) || hasEH(F))
    return false;

  Module *M = F.getParent();
  const DataLayout &DL = M->getDataLayout();
  LLVMContext &Ctx = F.getContext();

  SmallVector<AllocaInst *, 16> Allocas;
  for (Instruction &I : F.getEntryBlock()) {
    auto *AI = dyn_cast<AllocaInst>(&I);
    if (!AI)
      continue;
    if (isSupportedAlloca(*AI, DL))
      Allocas.push_back(AI);
  }
  if (Allocas.empty())
    return false;

  SmallVector<Type *, 32> FieldTypes;
  SmallVector<LocalSlot, 16> Slots;
  uint64_t CurrentOffset = 0;
  Align MaxAlign(1);

  for (AllocaInst *AI : Allocas) {
    Type *FieldTy = getFieldType(*AI);
    TypeSize FieldSize = DL.getTypeAllocSize(FieldTy);
    if (FieldSize.isScalable())
      return false;

    Align FieldAlign =
        std::max(AI->getAlign(), DL.getABITypeAlign(FieldTy));
    MaxAlign = std::max(MaxAlign, FieldAlign);

    uint64_t AlignedOffset = alignTo(CurrentOffset, FieldAlign);
    if (AlignedOffset != CurrentOffset) {
      FieldTypes.push_back(
          ArrayType::get(Type::getInt8Ty(Ctx), AlignedOffset - CurrentOffset));
      CurrentOffset = AlignedOffset;
    }

    LocalSlot Slot;
    Slot.Alloca = AI;
    Slot.FieldTy = FieldTy;
    Slot.FieldIndex = FieldTypes.size();
    Slot.IsArrayAllocation = AI->isArrayAllocation();
    Slots.push_back(Slot);

    FieldTypes.push_back(FieldTy);
    CurrentOffset += FieldSize.getFixedValue();
  }

  if (FieldTypes.empty())
    return false;

  StructType *StructTy =
      StructType::create(Ctx, FieldTypes,
                         (Twine("vllvm.localvars.") + F.getName()).str());

  Instruction *InsertBefore = &*F.getEntryBlock().getFirstInsertionPt();
  IRBuilder<> IRB(InsertBefore);

  Type *IntPtrTy = DL.getIntPtrType(Ctx);
  Constant *StructSize = ConstantExpr::getSizeOf(StructTy);
  StructSize = ConstantExpr::getTruncOrBitCast(StructSize, IntPtrTy);

  uint64_t ExtraAlignBytes = MaxAlign.value() - 1;
  Constant *AllocSize =
      ConstantExpr::getAdd(StructSize, getIntPtrConstant(IntPtrTy, ExtraAlignBytes));

  CallInst *RawStructPtr =
      IRB.CreateMalloc(IntPtrTy, StructTy, AllocSize, nullptr, nullptr,
                       "vllvm.locals.raw");
  Value *StructPtr = RawStructPtr;
  if (MaxAlign.value() > 1) {
    unsigned PtrBits = cast<IntegerType>(IntPtrTy)->getBitWidth();
    Value *RawInt = IRB.CreatePtrToInt(RawStructPtr, IntPtrTy, "vllvm.locals.int");
    Value *Biased = IRB.CreateAdd(
        RawInt, getIntPtrConstant(IntPtrTy, ExtraAlignBytes),
        "vllvm.locals.bias");
    Constant *Mask = ConstantInt::get(
        IntPtrTy, APInt(PtrBits, ~(MaxAlign.value() - 1)));
    Value *AlignedInt = IRB.CreateAnd(Biased, Mask, "vllvm.locals.aligned_int");
    StructPtr =
        IRB.CreateIntToPtr(AlignedInt, IRB.getPtrTy(), "vllvm.locals");
  }

  for (const LocalSlot &Slot : Slots) {
    std::string SlotName = Slot.Alloca->hasName()
                               ? (Slot.Alloca->getName() + ".slot").str()
                               : "vllvm.local.slot";
    Value *SlotPtr =
        IRB.CreateStructGEP(StructTy, StructPtr, Slot.FieldIndex, SlotName);
    if (Slot.IsArrayAllocation) {
      SlotPtr = IRB.CreateInBoundsGEP(
          Slot.FieldTy, SlotPtr,
          {getIntPtrConstant(IntPtrTy, 0), getIntPtrConstant(IntPtrTy, 0)},
          SlotName + ".elem");
    }
    Slot.Alloca->replaceAllUsesWith(SlotPtr);
  }

  for (LocalSlot &Slot : Slots)
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
