#include "LocalVarStructPass.h"

#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/Twine.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/Alignment.h"
#include "llvm/Support/RandomNumberGenerator.h"

#include <algorithm>
#include <cstdint>
#include <memory>
#include <optional>

using namespace llvm;

namespace {
// 记录原始栈槽与替代它的堆结构体字段之间的对应关系。
struct LocalSlot {
  AllocaInst *Alloca = nullptr;
  unsigned FieldIndex = 0;
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
  // 只处理固定大小、默认地址空间的 alloca。动态栈槽或 ABI 特殊栈槽可能
  // 带有额外 lowering 约束，不能直接替换成普通堆字段。
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
  // LLVM 中 "alloca T, N" 的结果仍是 T*；放进结构体时需要表达完整
  // 存储对象，所以多元素 alloca 会变成 [N x T] 字段。
  auto *ArraySize = cast<ConstantInt>(AI.getArraySize());
  Type *AllocatedTy = AI.getAllocatedType();
  if (ArraySize->isOne())
    return AllocatedTy;
  return ArrayType::get(AllocatedTy, ArraySize->getZExtValue());
}

Constant *getIntPtrConstant(Type *IntPtrTy, uint64_t Value) {
  return ConstantInt::get(IntPtrTy, Value);
}

uint32_t makeNonZeroKey(RandomNumberGenerator &RNG, unsigned SlotNo) {
  uint32_t Key = static_cast<uint32_t>(RNG());
  if (Key != 0)
    return Key;

  Key = static_cast<uint32_t>(RNG());
  if (Key != 0)
    return Key;

  return 0xA5A5A5A5U ^ static_cast<uint32_t>(SlotNo + 1);
}

Instruction *getInsertionPointForUse(Use &U) {
  User *TheUser = U.getUser();
  if (auto *PN = dyn_cast<PHINode>(TheUser)) {
    BasicBlock *IncomingBB = PN->getIncomingBlock(U.getOperandNo());
    return IncomingBB->getTerminator();
  }

  return dyn_cast<Instruction>(TheUser);
}
} // namespace

PreservedAnalyses LocalVarStructPass::run(Function &F,
                                          FunctionAnalysisManager &FAM) {
  errs() << "[vllvm] LocalVarStructPass:" << F.getName() << "\n";
  bool IsChanged = moveAllocasToStruct(F);
  return IsChanged ? PreservedAnalyses::none() : PreservedAnalyses::all();
}

bool LocalVarStructPass::moveAllocasToStruct(Function &F) {
  // 这些场景插入 malloc/free 容易破坏 ABI 敏感控制流或异常处理语义，
  // 因此先保守跳过。
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

  for (AllocaInst *AI : Allocas)
    for (Use &U : AI->uses())
      if (!getInsertionPointForUse(U))
        return false;

  SmallVector<Type *, 32> FieldTypes;
  SmallVector<LocalSlot, 16> Slots;
  uint64_t CurrentOffset = 0;
  Align MaxAlign(1);

  // 手工构造结构体布局；必要时插入 i8 padding 字段，保证每个旧 alloca
  // 至少保留原来的对齐要求。
  for (AllocaInst *AI : Allocas) {
    Type *FieldTy = getFieldType(*AI);
    TypeSize FieldSize = DL.getTypeAllocSize(FieldTy);
    if (FieldSize.isScalable())
      return false;

    Align FieldAlign = std::max(AI->getAlign(), DL.getABITypeAlign(FieldTy));
    MaxAlign = std::max(MaxAlign, FieldAlign);

    uint64_t AlignedOffset = alignTo(CurrentOffset, FieldAlign);
    if (AlignedOffset != CurrentOffset) {
      FieldTypes.push_back(
          ArrayType::get(Type::getInt8Ty(Ctx), AlignedOffset - CurrentOffset));
      CurrentOffset = AlignedOffset;
    }

    LocalSlot Slot;
    Slot.Alloca = AI;
    Slot.FieldIndex = FieldTypes.size();
    Slots.push_back(Slot);

    FieldTypes.push_back(FieldTy);
    CurrentOffset += FieldSize.getFixedValue();
  }

  if (FieldTypes.empty())
    return false;

  StructType *StructTy = StructType::create(
      Ctx, FieldTypes, (Twine("vllvm.localvars.") + F.getName()).str());
  const StructLayout *Layout = DL.getStructLayout(StructTy);

  Type *IntPtrTy = DL.getIntPtrType(Ctx);
  unsigned PtrBits = cast<IntegerType>(IntPtrTy)->getBitWidth();
  Type *ConstTy = Type::getInt32Ty(Ctx);
  ArrayType *ConstTableTy = ArrayType::get(ConstTy, Slots.size());
  SmallVector<Constant *, 16> ConstEntries;
  SmallVector<uint32_t, 16> OffsetKeys;
  std::unique_ptr<RandomNumberGenerator> RNG =
      M->createRNG((Twine("vllvm.localvars.") + F.getName()).str());

  // 每个函数一张可写数据表，表里只保存加密后的字段偏移。key 是按 slot
  // 随机生成的局部常量，不进入全局数据。
  for (size_t SlotNo = 0; SlotNo < Slots.size(); ++SlotNo) {
    const LocalSlot &Slot = Slots[SlotNo];
    uint64_t Offset = Layout->getElementOffset(Slot.FieldIndex);
    if (Offset > UINT32_MAX)
      return false;

    uint32_t OffsetKey = makeNonZeroKey(*RNG, SlotNo);
    uint32_t EncryptedOffset = static_cast<uint32_t>(Offset) ^ OffsetKey;

    OffsetKeys.push_back(OffsetKey);
    ConstEntries.push_back(ConstantInt::get(ConstTy, EncryptedOffset));
  }

  GlobalVariable *ConstTable =
      new GlobalVariable(*M, ConstTableTy, false, GlobalValue::PrivateLinkage,
                         ConstantArray::get(ConstTableTy, ConstEntries),
                         (Twine("vllvm.localvars.table.") + F.getName()).str());
  ConstTable->setAlignment(DL.getABITypeAlign(ConstTableTy));

  Instruction *InsertBefore = &*F.getEntryBlock().getFirstInsertionPt();
  IRBuilder<> First_IRB(InsertBefore);

  Constant *StructSize = ConstantExpr::getSizeOf(StructTy);
  StructSize = ConstantExpr::getTruncOrBitCast(StructSize, IntPtrTy);

  // malloc 通常只保证平台默认对齐。这里额外申请 MaxAlign - 1 字节并
  // 手动向上对齐；释放时仍使用原始 malloc 指针。
  uint64_t ExtraAlignBytes = MaxAlign.value() - 1;
  Constant *AllocSize = ConstantExpr::getAdd(
      StructSize, getIntPtrConstant(IntPtrTy, ExtraAlignBytes));

  CallInst *RawStructPtr = First_IRB.CreateMalloc(
      IntPtrTy, StructTy, AllocSize, nullptr, nullptr, "vllvm.locals.raw");
  Value *StructPtr = RawStructPtr;
  if (MaxAlign.value() > 1) {
    Value *RawInt =
        First_IRB.CreatePtrToInt(RawStructPtr, IntPtrTy, "vllvm.locals.int");
    Value *Biased = First_IRB.CreateAdd(
        RawInt, getIntPtrConstant(IntPtrTy, ExtraAlignBytes),
        "vllvm.locals.bias");
    Constant *Mask =
        ConstantInt::get(IntPtrTy, APInt(PtrBits, ~(MaxAlign.value() - 1)));
    Value *AlignedInt =
        First_IRB.CreateAnd(Biased, Mask, "vllvm.locals.aligned_int");
    StructPtr = First_IRB.CreateIntToPtr(AlignedInt, First_IRB.getPtrTy(),
                                         "vllvm.locals");
  }

  // 把旧栈槽的所有引用改写成“结构体基址 + 表中解密出的偏移”。
  for (size_t SlotNo = 0; SlotNo < Slots.size(); ++SlotNo) {
    const LocalSlot &Slot = Slots[SlotNo];
    std::string SlotName = Slot.Alloca->hasName()
                               ? (Slot.Alloca->getName() + ".slot").str()
                               : "vllvm.local.slot";

    SmallVector<Use *, 16> Uses;
    for (Use &U : Slot.Alloca->uses())
      Uses.push_back(&U);

    for (Use *U : Uses) {
      Instruction *InsertPt = getInsertionPointForUse(*U);
      IRBuilder<> UseIRB(InsertPt);
      Value *ConstEntryPtr = UseIRB.CreateGEP(
          ConstTableTy, ConstTable,
          {ConstantInt::get(Type::getInt32Ty(Ctx), 0),
           ConstantInt::get(Type::getInt32Ty(Ctx), SlotNo)},
          "vllvm.local.const.entry");
      LoadInst *EncryptedOffset =
          UseIRB.CreateLoad(ConstTy, ConstEntryPtr, "vllvm.local.enc_offset");
      EncryptedOffset->setVolatile(true);
      Constant *OffsetKey = ConstantInt::get(ConstTy, OffsetKeys[SlotNo]);
      Value *Offset32 =
          UseIRB.CreateXor(EncryptedOffset, OffsetKey, "vllvm.local.offset32");
      Value *Offset =
          UseIRB.CreateZExtOrBitCast(Offset32, IntPtrTy, "vllvm.local.offset");
      Value *SlotPtr =
          UseIRB.CreateGEP(First_IRB.getInt8Ty(), StructPtr, Offset, SlotName);
      U->set(SlotPtr);
    }
  }

  for (LocalSlot &Slot : Slots)
    Slot.Alloca->eraseFromParent();

  SmallVector<ReturnInst *, 8> Returns;
  for (BasicBlock &BB : F)
    if (auto *RI = dyn_cast<ReturnInst>(BB.getTerminator()))
      Returns.push_back(RI);

  // 在每条普通 return 路径前释放原始 malloc 指针；对齐后的指针可能已经
  // 发生偏移，不能直接传给 free。
  for (ReturnInst *RI : Returns) {
    IRBuilder<> FreeIRB(RI);
    FreeIRB.CreateFree(RawStructPtr);
  }

  return true;
}
