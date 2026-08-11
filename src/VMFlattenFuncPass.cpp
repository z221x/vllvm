#include "VMFlattenFuncPass.h"

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
#include <utility>
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
  Value *TableBase = nullptr;

  SharedConstTable(Function &Fn)
      : F(Fn), M(*Fn.getParent()), DL(M.getDataLayout()), Ctx(Fn.getContext()),
        ConstTy(Type::getInt32Ty(Ctx)) {}

  unsigned size() const { return Entries.size(); }
  bool empty() const { return Entries.empty(); }

  unsigned add(uint32_t Value) {
    Entries.push_back(ConstantInt::get(ConstTy, Value));
    return Entries.size() - 1;
  }

  GlobalVariable *createGlobal(const Twine &Name,
                               ArrayRef<Constant *> InitEntries) {
    ArrayType *TableTy = ArrayType::get(ConstTy, InitEntries.size());
    auto *NewGV = new GlobalVariable(
        M, TableTy, false, GlobalValue::PrivateLinkage,
        ConstantArray::get(TableTy, InitEntries), Name.str());
    NewGV->setAlignment(DL.getABITypeAlign(TableTy));
    return NewGV;
  }

  void ensureMaterialized() {
    if (GV || Entries.empty())
      return;
    GV = createGlobal((Twine("vllvm.vmfla.const.table.tmp.") + F.getName()),
                      Entries);
  }

  void finalize() {
    if (Entries.empty())
      return;

    GlobalVariable *FinalGV = createGlobal(
        (Twine("vllvm.vmfla.const.table.") + F.getName()), Entries);
    if (GV) {
      GV->replaceAllUsesWith(FinalGV);
      GV->eraseFromParent();
    }
    GV = FinalGV;
  }

  GlobalVariable *getGlobal() {
    ensureMaterialized();
    return GV;
  }

  void setTableBase(Value *Base) { TableBase = Base; }

  // BCF 在创建 impl 前会先插入常量表访问；函数体搬入 impl 后，
  // 这些访问需要从临时全局表改成读取 wrapper 传入的表参数。
  void rebaseUsesInFunction(Function &Fn) {
    if (!GV || !TableBase)
      return;

    SmallVector<Use *, 16> Uses;
    for (Use &U : GV->uses()) {
      auto *I = dyn_cast<Instruction>(U.getUser());
      if (I && I->getFunction() == &Fn)
        Uses.push_back(&U);
    }

    for (Use *U : Uses)
      U->set(TableBase);
  }

  Value *getEntryPtr(IRBuilder<> &IRB, Value *Index, const Twine &Name) {
    ensureMaterialized();
    Value *Base = TableBase ? TableBase : GV;
    Value *Indexes[] = {Index};
    // 显式创建 GEP 指令，避免常量索引访问被折叠成 ConstantExpr，
    // 否则后续 rebase 到 impl 参数时无法替换。
    auto *GEP = GetElementPtrInst::Create(ConstTy, Base, Indexes, Name);
    return IRB.Insert(GEP);
  }

  Value *getEntryPtr(IRBuilder<> &IRB, unsigned Index, const Twine &Name) {
    return getEntryPtr(IRB, ConstantInt::get(Type::getInt32Ty(Ctx), Index),
                       Name);
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
  AllocaInst *RuntimeIndexAlloca = nullptr;
  Value *RuntimeIndexPtr = nullptr;
};

struct IndirectCallPlan {
  bool Enabled = false;
  std::vector<Function *> Callees;
  DenseMap<Function *, unsigned> CalleeNums;
  std::vector<CallInst *> CallSites;
  DenseMap<CallInst *, unsigned> CallSiteConstIndexes;
  DenseMap<CallInst *, unsigned> CallSiteKeyConstIndexes;
};

struct LocalSlot {
  AllocaInst *Alloca = nullptr;
  unsigned FieldIndex = 0;
  unsigned ConstIndex = 0;
  unsigned KeyConstIndex = 0;
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

std::optional<unsigned> findStateIndexForBB(const FlattenPlan &Plan,
                                            BasicBlock *TargetBB) {
  for (unsigned I = 0; I < Plan.FlattenBBs.size(); ++I)
    if (Plan.FlattenBBs[I] == TargetBB)
      return I;
  return std::nullopt;
}

std::optional<unsigned> findCaseConstIndexForBB(const FlattenPlan &Plan,
                                                BasicBlock *TargetBB) {
  std::optional<unsigned> StateIndex = findStateIndexForBB(Plan, TargetBB);
  if (!StateIndex)
    return std::nullopt;
  return Plan.CaseConstIndexes[*StateIndex];
}

LoadInst *loadFlattenRuntimeIndex(IRBuilder<> &IRB, Value *RuntimeIndexPtr,
                                  Type *Int32Ty, const Twine &Name) {
  LoadInst *Loaded = IRB.CreateLoad(Int32Ty, RuntimeIndexPtr, Name);
  Loaded->setVolatile(true);
  return Loaded;
}

StoreInst *storeFlattenRuntimeIndex(IRBuilder<> &IRB,
                                    Value *RuntimeIndexPtr, Value *Index) {
  StoreInst *Store = IRB.CreateStore(Index, RuntimeIndexPtr);
  Store->setVolatile(true);
  return Store;
}

Value *moveFromCurrentTableIndex(IRBuilder<> &IRB, Value *CurrentIndex,
                                 Type *Int32Ty, unsigned CurrentConstIndex,
                                 unsigned TargetConstIndex,
                                 const Twine &Name) {
  if (CurrentConstIndex == TargetConstIndex)
    return CurrentIndex;

  if (TargetConstIndex > CurrentConstIndex) {
    return IRB.CreateAdd(
        CurrentIndex,
        ConstantInt::get(Int32Ty, TargetConstIndex - CurrentConstIndex), Name);
  }

  return IRB.CreateSub(
      CurrentIndex,
      ConstantInt::get(Int32Ty, CurrentConstIndex - TargetConstIndex), Name);
}

// icall/lvars 的常量项不直接用固定下标取；优先从当前 flatten case
// 的表下标加减偏移得到目标下标，让运行时只围绕这一个 index 变化。
Value *loadConstViaFlattenIndex(IRBuilder<> &IRB, SharedConstTable &ConstTable,
                                const FlattenPlan *Plan,
                                unsigned TargetConstIndex,
                                const Twine &Name) {
  if (!Plan || !Plan->Enabled || !Plan->RuntimeIndexPtr)
    return ConstTable.load(IRB, TargetConstIndex, Name);

  BasicBlock *BB = IRB.GetInsertBlock();
  std::optional<unsigned> AnchorConstIndex =
      findCaseConstIndexForBB(*Plan, BB);
  if (!AnchorConstIndex && BB == &BB->getParent()->getEntryBlock())
    AnchorConstIndex = Plan->CaseConstIndexes[Plan->InitialStateIndex];
  if (!AnchorConstIndex)
    return ConstTable.load(IRB, TargetConstIndex, Name);

  Type *Int32Ty = Type::getInt32Ty(BB->getContext());
  Value *CurrentIndex =
      loadFlattenRuntimeIndex(IRB, Plan->RuntimeIndexPtr, Int32Ty,
                              Name + ".runtime_index");
  Value *TableIndex = moveFromCurrentTableIndex(
      IRB, CurrentIndex, Int32Ty, *AnchorConstIndex, TargetConstIndex,
      Name + ".table_index");
  return ConstTable.load(IRB, TableIndex, Name);
}

bool isFlattenableBlock(BasicBlock &BB) {
  Instruction *Term = BB.getTerminator();
  if (!Term || BB.isEHPad())
    return false;

  // EH pad/indirect terminator 不能作为状态机 case 重写；invoke 单独处理，
  // 只改 normal 边，unwind 边保持原状。
  if (isa<IndirectBrInst>(Term) || isa<CallBrInst>(Term) ||
      isa<CatchSwitchInst>(Term) || isa<CatchReturnInst>(Term) ||
      isa<CleanupReturnInst>(Term) || isa<ResumeInst>(Term))
    return false;

  return true;
}

void updatePhiIncomingBlock(BasicBlock *SuccBB, BasicBlock *OldPred,
                            BasicBlock *NewPred) {
  for (PHINode &PN : SuccBB->phis()) {
    for (unsigned I = 0, E = PN.getNumIncomingValues(); I != E; ++I)
      if (PN.getIncomingBlock(I) == OldPred)
        PN.setIncomingBlock(I, NewPred);
  }
}

uint32_t makeTableIndexDelta(unsigned CurrentTableIndex,
                             unsigned TargetTableIndex) {
  return static_cast<uint32_t>(TargetTableIndex) -
         static_cast<uint32_t>(CurrentTableIndex);
}

void addFlattenStateDelta(IRBuilder<> &IRB, Value *SwitchVar,
                          Type *Int32Ty, Value *StateDelta) {
  Value *OldStateIndex =
      loadFlattenRuntimeIndex(IRB, SwitchVar, Int32Ty, "oldTableIndex");
  Value *NewStateIndex =
      IRB.CreateAdd(OldStateIndex, StateDelta, "newTableIndex");
  storeFlattenRuntimeIndex(IRB, SwitchVar, NewStateIndex);
}

void addFlattenStateDelta(IRBuilder<> &IRB, Value *SwitchVar,
                          Type *Int32Ty, unsigned CurrentTableIndex,
                          unsigned TargetTableIndex) {
  Value *StateDelta = ConstantInt::get(
      Int32Ty, makeTableIndexDelta(CurrentTableIndex, TargetTableIndex));
  addFlattenStateDelta(IRB, SwitchVar, Int32Ty, StateDelta);
}

BasicBlock *createFlattenStateBlock(Function &F, BasicBlock *InsertBefore,
                                    Value *SwitchVar, Type *Int32Ty,
                                    unsigned CurrentTableIndex,
                                    unsigned TargetTableIndex,
                                    BasicBlock *SwitchLoopEntry,
                                    const Twine &Name) {
  BasicBlock *StateBB =
      BasicBlock::Create(F.getContext(), Name, &F, InsertBefore);
  IRBuilder<> IRB(StateBB);
  addFlattenStateDelta(IRB, SwitchVar, Int32Ty, CurrentTableIndex,
                       TargetTableIndex);
  IRB.CreateBr(SwitchLoopEntry);
  return StateBB;
}

Instruction *getRuntimeIndexInitPoint(Function &F, FlattenPlan &Plan) {
  if (auto *I = dyn_cast_or_null<Instruction>(Plan.RuntimeIndexPtr)) {
    if (I->getFunction() == &F && I->getNextNode())
      return I->getNextNode();
  }

  return &*F.getEntryBlock().getFirstInsertionPt();
}

Value *ensureFlattenRuntimeIndex(Function &F, FlattenPlan &Plan,
                                 Type *Int32Ty) {
  if (!Plan.Enabled)
    return nullptr;

  if (!Plan.RuntimeIndexPtr) {
    Instruction *InsertPt = &*F.getEntryBlock().getFirstInsertionPt();
    IRBuilder<> IRB(InsertPt);
    AllocaInst *SwitchVar = IRB.CreateAlloca(Int32Ty, 0, "switchVar");
    Plan.RuntimeIndexAlloca = SwitchVar;
    Plan.RuntimeIndexPtr = SwitchVar;
  }

  IRBuilder<> IRB(getRuntimeIndexInitPoint(F, Plan));
  storeFlattenRuntimeIndex(
      IRB, Plan.RuntimeIndexPtr,
      ConstantInt::get(Int32Ty, Plan.CaseConstIndexes[Plan.InitialStateIndex]));
  return Plan.RuntimeIndexPtr;
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

// vmfla 模式下的 BCF 使用随机覆盖率，避免每个块都生成同构 fake 分支。
constexpr unsigned SharedBogusMinProbability = 55;
constexpr unsigned SharedBogusProbabilityRange = 36;
constexpr unsigned SharedBogusLoops = 1;

bool isSharedBogusGeneratedBlock(BasicBlock &BB) {
  return BB.hasName() && BB.getName().starts_with("vllvm.bcf.");
}

bool hasMustTailReturn(BasicBlock &BB) {
  auto *RI = dyn_cast<ReturnInst>(BB.getTerminator());
  if (!RI)
    return false;

  Instruction *Prev = RI->getPrevNonDebugInstruction();
  auto *CB = dyn_cast_or_null<CallBase>(Prev);
  return CB && CB->isMustTailCall();
}

bool canSplitForSharedBogusFlow(BasicBlock &BB) {
  Instruction *Term = BB.getTerminator();
  if (!Term || isSharedBogusGeneratedBlock(BB) || BB.isEHPad() ||
      hasMustTailReturn(BB))
    return false;

  // C++ EH 函数可以做 BCF，但 landingpad/resume/catchret 等 EH 专用块
  // 不能被 fake 分支包裹；invoke 本身允许保留在 split 后的 tail 块里。
  if (isa<IndirectBrInst>(Term) || isa<CallBrInst>(Term) ||
      isa<CatchSwitchInst>(Term) || isa<CatchReturnInst>(Term) ||
      isa<CleanupReturnInst>(Term) || isa<ResumeInst>(Term))
    return false;

  return true;
}

Value *loadSharedBogusConstant(IRBuilder<> &IRB, SharedConstTable &ConstTable,
                               uint32_t Value, const Twine &Name) {
  // BCF 的随机常量也放入同一张函数级常量表，避免额外散落全局变量。
  return ConstTable.load(IRB, ConstTable.add(Value), Name);
}

Value *createSharedOpaquePredicate(IRBuilder<> &IRB,
                                   SharedConstTable &ConstTable,
                                   unsigned XIndex, unsigned YIndex,
                                   uint32_t XSeed, uint32_t YSeed,
                                   CryptoUtils &Crypto,
                                   bool &PredicateIsTrue) {
  Value *XVal = ConstTable.load(IRB, XIndex, "vllvm.bcf.x");
  Value *YVal = ConstTable.load(IRB, YIndex, "vllvm.bcf.y");
  Value *Expr = (Crypto.getRandom32() & 1) ? XVal : YVal;
  uint32_t Expected = Expr == XVal ? XSeed : YSeed;

  auto Other = [&](bool UseX) -> std::pair<Value *, uint32_t> {
    return UseX ? std::make_pair(XVal, XSeed) : std::make_pair(YVal, YSeed);
  };

  unsigned Rounds = 4 + (Crypto.getRandom32() % 5);
  for (unsigned I = 0; I < Rounds; ++I) {
    uint32_t K = Crypto.getRandom32();
    switch (Crypto.getRandom32() % 6) {
    case 0: {
      Value *KVal = loadSharedBogusConstant(IRB, ConstTable, K, "vllvm.bcf.k");
      Expr = IRB.CreateAdd(Expr, KVal, "vllvm.bcf.mix.add");
      Expected += K;
      break;
    }
    case 1: {
      Value *KVal = loadSharedBogusConstant(IRB, ConstTable, K, "vllvm.bcf.k");
      Expr = IRB.CreateSub(Expr, KVal, "vllvm.bcf.mix.sub");
      Expected -= K;
      break;
    }
    case 2: {
      K |= 1U;
      Value *KVal = loadSharedBogusConstant(IRB, ConstTable, K, "vllvm.bcf.k");
      Expr = IRB.CreateMul(Expr, KVal, "vllvm.bcf.mix.mul");
      Expected *= K;
      break;
    }
    case 3: {
      Value *KVal = loadSharedBogusConstant(IRB, ConstTable, K, "vllvm.bcf.k");
      Expr = IRB.CreateXor(Expr, KVal, "vllvm.bcf.mix.xor");
      Expected ^= K;
      break;
    }
    case 4: {
      auto [OtherVal, OtherExpected] = Other(Crypto.getRandom32() & 1);
      Expr = IRB.CreateAdd(Expr, OtherVal, "vllvm.bcf.mix.addv");
      Expected += OtherExpected;
      break;
    }
    default: {
      auto [OtherVal, OtherExpected] = Other(Crypto.getRandom32() & 1);
      Expr = IRB.CreateXor(Expr, OtherVal, "vllvm.bcf.mix.xorv");
      Expected ^= OtherExpected;
      break;
    }
    }
  }

  bool Negate = (Crypto.getRandom32() & 1) != 0;
  PredicateIsTrue = !Negate;
  Value *ExpectedValue =
      loadSharedBogusConstant(IRB, ConstTable, Expected, "vllvm.bcf.expected");
  return Negate ? IRB.CreateICmpNE(Expr, ExpectedValue, "vllvm.bcf.pred")
                : IRB.CreateICmpEQ(Expr, ExpectedValue, "vllvm.bcf.pred");
}

void insertSharedBogusJunk(IRBuilder<> &IRB, SharedConstTable &ConstTable,
                           unsigned XIndex, unsigned YIndex,
                           CryptoUtils &Crypto) {
  Value *A = ConstTable.load(IRB, XIndex, "vllvm.bcf.fake.x");
  Value *B = ConstTable.load(IRB, YIndex, "vllvm.bcf.fake.y");

  Value *Junk = IRB.CreateXor(A, B, "vllvm.bcf.fake.mix");
  Junk = IRB.CreateAdd(
      Junk,
      loadSharedBogusConstant(IRB, ConstTable, Crypto.getRandom32(),
                              "vllvm.bcf.fake.k"),
      "vllvm.bcf.fake.add");
  Junk = IRB.CreateMul(
      Junk,
      loadSharedBogusConstant(IRB, ConstTable, Crypto.getRandom32() | 1U,
                              "vllvm.bcf.fake.k"),
      "vllvm.bcf.fake.mul");

  Value *XPtr = ConstTable.getEntryPtr(IRB, XIndex, "vllvm.bcf.fake.x");
  StoreInst *Store = IRB.CreateStore(Junk, XPtr);
  Store->setVolatile(true);
}

void terminateSharedFakePath(BasicBlock *Fake, BasicBlock *Tail,
                             SharedConstTable &ConstTable, unsigned XIndex,
                             unsigned YIndex, uint32_t XSeed, uint32_t YSeed,
                             CryptoUtils &Crypto) {
  Function *F = Fake->getParent();
  LLVMContext &Ctx = F->getContext();

  auto CreatePredicate = [&](IRBuilder<> &IRB) {
    bool PredicateIsTrue = true;
    return createSharedOpaquePredicate(IRB, ConstTable, XIndex, YIndex, XSeed,
                                       YSeed, Crypto, PredicateIsTrue);
  };

  // fake 路径不能总是 fake -> tail；随机短链、分叉和自环能降低 CFG 模板感。
  switch (Crypto.getRandom32() % 4) {
  case 0: {
    IRBuilder<> FakeIRB(Fake);
    insertSharedBogusJunk(FakeIRB, ConstTable, XIndex, YIndex, Crypto);
    FakeIRB.CreateBr(Tail);
    return;
  }
  case 1: {
    BasicBlock *Next =
        BasicBlock::Create(Ctx, "vllvm.bcf.fake.next", F, Tail->getNextNode());
    IRBuilder<> FakeIRB(Fake);
    insertSharedBogusJunk(FakeIRB, ConstTable, XIndex, YIndex, Crypto);
    FakeIRB.CreateCondBr(CreatePredicate(FakeIRB), Next, Tail);

    IRBuilder<> NextIRB(Next);
    insertSharedBogusJunk(NextIRB, ConstTable, XIndex, YIndex, Crypto);
    NextIRB.CreateBr(Tail);
    return;
  }
  case 2: {
    BasicBlock *Left =
        BasicBlock::Create(Ctx, "vllvm.bcf.fake.left", F, Tail->getNextNode());
    BasicBlock *Right =
        BasicBlock::Create(Ctx, "vllvm.bcf.fake.right", F, Tail->getNextNode());
    IRBuilder<> FakeIRB(Fake);
    insertSharedBogusJunk(FakeIRB, ConstTable, XIndex, YIndex, Crypto);
    FakeIRB.CreateCondBr(CreatePredicate(FakeIRB), Left, Right);

    IRBuilder<> LeftIRB(Left);
    insertSharedBogusJunk(LeftIRB, ConstTable, XIndex, YIndex, Crypto);
    LeftIRB.CreateBr(Tail);

    IRBuilder<> RightIRB(Right);
    insertSharedBogusJunk(RightIRB, ConstTable, XIndex, YIndex, Crypto);
    RightIRB.CreateBr(Tail);
    return;
  }
  default: {
    BasicBlock *Loop =
        BasicBlock::Create(Ctx, "vllvm.bcf.fake.loop", F, Tail->getNextNode());
    IRBuilder<> FakeIRB(Fake);
    insertSharedBogusJunk(FakeIRB, ConstTable, XIndex, YIndex, Crypto);
    FakeIRB.CreateCondBr(CreatePredicate(FakeIRB), Loop, Tail);

    IRBuilder<> LoopIRB(Loop);
    insertSharedBogusJunk(LoopIRB, ConstTable, XIndex, YIndex, Crypto);
    Value *KeepLooping = CreatePredicate(LoopIRB);
    if (Crypto.getRandom32() & 1)
      LoopIRB.CreateCondBr(KeepLooping, Tail, Loop);
    else
      LoopIRB.CreateCondBr(KeepLooping, Loop, Tail);
    return;
  }
  }
}

bool addSharedBogusFlow(BasicBlock &BB, SharedConstTable &ConstTable,
                        unsigned XIndex, unsigned YIndex, uint32_t XSeed,
                        uint32_t YSeed, CryptoUtils &Crypto) {
  Instruction *Term = BB.getTerminator();
  if (!Term || !canSplitForSharedBogusFlow(BB))
    return false;

  Function *F = BB.getParent();
  LLVMContext &Ctx = F->getContext();

  BasicBlock *Tail = BB.splitBasicBlock(Term->getIterator(), "vllvm.bcf.tail");
  BasicBlock *Fake =
      BasicBlock::Create(Ctx, "vllvm.bcf.fake", F, Tail->getNextNode());

  BB.getTerminator()->eraseFromParent();

  IRBuilder<> RealIRB(&BB);
  bool PredicateIsTrue = true;
  Value *Predicate =
      createSharedOpaquePredicate(RealIRB, ConstTable, XIndex, YIndex, XSeed,
                                  YSeed, Crypto, PredicateIsTrue);
  BasicBlock *TrueBB = PredicateIsTrue ? Tail : Fake;
  BasicBlock *FalseBB = PredicateIsTrue ? Fake : Tail;
  RealIRB.CreateCondBr(Predicate, TrueBB, FalseBB);

  terminateSharedFakePath(Fake, Tail, ConstTable, XIndex, YIndex, XSeed, YSeed,
                          Crypto);
  return true;
}

bool applySharedBogusControlFlow(Function &F, SharedConstTable &ConstTable,
                                 CryptoUtils &Crypto) {
  if (F.empty() || F.isDeclaration() || F.hasFnAttribute(Attribute::Naked))
    return false;

  SmallVector<BasicBlock *, 32> Candidates;
  SmallVector<BasicBlock *, 32> Targets;
  unsigned Probability = SharedBogusMinProbability +
                         (Crypto.getRandom32() % SharedBogusProbabilityRange);
  // 不再 100% 覆盖所有块；随机命中能减少“每块一根横线”的图形指纹。
  for (unsigned Loop = 0; Loop < SharedBogusLoops; ++Loop) {
    for (BasicBlock &BB : F) {
      if (!canSplitForSharedBogusFlow(BB))
        continue;
      Candidates.push_back(&BB);
      if (Crypto.getRandom32() % 100 < Probability)
        Targets.push_back(&BB);
    }
  }
  // 小函数也至少命中一个可拆基本块，避免显式启用 bcf 却完全无变化。
  if (Targets.empty() && !Candidates.empty())
    Targets.push_back(Candidates[Crypto.getRandom32() % Candidates.size()]);
  if (Targets.empty())
    return false;

  uint32_t XSeed = Crypto.getRandom32();
  uint32_t YSeed = Crypto.getRandom32();
  unsigned XIndex = ConstTable.add(XSeed);
  unsigned YIndex = ConstTable.add(YSeed);

  bool Changed = false;
  for (BasicBlock *BB : Targets)
    if (BB->getParent() == &F)
      Changed |= addSharedBogusFlow(*BB, ConstTable, XIndex, YIndex, XSeed,
                                    YSeed, Crypto);

  if (Changed)
    fixStackForFlatten(&F);
  return Changed;
}

// 给 dispatch 树选择带随机扰动的切分点，避免每次都形成完全平衡的固定形态。
unsigned chooseDispatchSplit(unsigned Begin, unsigned End, CryptoUtils &Crypto) {
  unsigned Count = End - Begin;
  if (Count <= 2)
    return Begin + 1;

  unsigned Base = Count / 2;
  unsigned Window = std::max(1U, Count / 4);
  unsigned Jitter = Crypto.getRandom32() % (Window * 2 + 1);
  int SignedSplit = static_cast<int>(Begin + Base) +
                    static_cast<int>(Jitter) - static_cast<int>(Window);
  unsigned Split = static_cast<unsigned>(
      std::clamp(SignedSplit, static_cast<int>(Begin + 1),
                 static_cast<int>(End - 1)));
  return Split;
}

// 将 flatten 的线性 case 链改成随机切分的二叉路由树，避免 CFG 被排成规则阶梯。
BasicBlock *buildDispatchTree(Function &F, BasicBlock *InsertBefore,
                              Value *StateTableIndex, Value *CaseValue,
                              Type *Int32Ty, const FlattenPlan &Plan,
                              SharedConstTable &ConstTable, unsigned Begin,
                              unsigned End, BasicBlock *DefaultBB,
                              CryptoUtils &Crypto) {
  LLVMContext &Ctx = F.getContext();
  BasicBlock *Node = BasicBlock::Create(
      Ctx, End - Begin == 1 ? "caseDispatch" : "caseRoute", &F, InsertBefore);
  IRBuilder<> IRB(Node);

  if (End - Begin == 1) {
    Value *CaseConst =
        ConstTable.load(IRB, Plan.CaseConstIndexes[Begin], "loadCaseConst");
    Value *CaseMatch = IRB.CreateICmpEQ(CaseValue, CaseConst, "caseConstMatch");
    IRB.CreateCondBr(CaseMatch, Plan.FlattenBBs[Begin], DefaultBB);
    return Node;
  }

  unsigned Split = chooseDispatchSplit(Begin, End, Crypto);
  BasicBlock *Left = buildDispatchTree(F, InsertBefore, StateTableIndex,
                                       CaseValue, Int32Ty, Plan, ConstTable,
                                       Begin, Split, DefaultBB, Crypto);
  BasicBlock *Right = buildDispatchTree(F, InsertBefore, StateTableIndex,
                                        CaseValue, Int32Ty, Plan, ConstTable,
                                        Split, End, DefaultBB, Crypto);
  Value *Threshold = ConstantInt::get(Int32Ty, Plan.CaseConstIndexes[Split]);
  if (Crypto.getRandom32() & 1) {
    Value *GoLeft =
        IRB.CreateICmpULT(StateTableIndex, Threshold, "dispatch.left");
    IRB.CreateCondBr(GoLeft, Left, Right);
  } else {
    Value *GoRight =
        IRB.CreateICmpUGE(StateTableIndex, Threshold, "dispatch.right");
    IRB.CreateCondBr(GoRight, Right, Left);
  }
  return Node;
}

// 随机调整基本块物理顺序，降低反编译器按块顺序布局时的规整感。
void shuffleFunctionBlocks(Function &F, CryptoUtils &Crypto) {
  if (F.size() <= 2)
    return;

  SmallVector<BasicBlock *, 64> Blocks;
  BasicBlock *Entry = &F.getEntryBlock();
  for (BasicBlock &BB : F)
    if (&BB != Entry)
      Blocks.push_back(&BB);

  std::default_random_engine ShuffleEngine(Crypto.getRandom32());
  std::shuffle(Blocks.begin(), Blocks.end(), ShuffleEngine);

  BasicBlock *InsertAfter = Entry;
  for (BasicBlock *BB : Blocks) {
    if (BB == InsertAfter)
      continue;
    BB->moveAfter(InsertAfter);
    InsertAfter = BB;
  }
}

void removeVLLVMAttributes(Function &F) {
  F.removeFnAttr("vllvm.obfuscate");
  F.removeFnAttr("vllvm.enstr");
  F.removeFnAttr("vllvm.vmfla");
  F.removeFnAttr("vllvm.fla");
  F.removeFnAttr("vllvm.icall");
  F.removeFnAttr("vllvm.ibr");
  F.removeFnAttr("vllvm.lvars");
  F.removeFnAttr("vllvm.bcf");
}

void collectParamAttrsWithTable(Function &F,
                                SmallVectorImpl<AttributeSet> &ParamAttrs) {
  AttributeList Attrs = F.getAttributes();
  ParamAttrs.clear();
  ParamAttrs.reserve(F.arg_size() + 1);
  for (unsigned I = 0, E = F.arg_size(); I != E; ++I)
    ParamAttrs.push_back(Attrs.getParamAttrs(I));
  ParamAttrs.push_back(AttributeSet());
}

Function *moveBodyToTableParamImpl(Function &F, SharedConstTable &ConstTable) {
  FunctionType *OldTy = F.getFunctionType();
  if (OldTy->isVarArg() || ConstTable.empty())
    return nullptr;

  Module *M = F.getParent();
  LLVMContext &Ctx = F.getContext();
  SmallVector<Type *, 16> Params(OldTy->param_begin(), OldTy->param_end());
  Params.push_back(PointerType::getUnqual(Ctx));

  FunctionType *ImplTy =
      FunctionType::get(OldTy->getReturnType(), Params, false);
  Function *Impl =
      Function::Create(ImplTy, GlobalValue::PrivateLinkage,
                       F.getAddressSpace(), F.getName() + ".vllvm.impl", M);

  Impl->copyAttributesFrom(&F);
  Impl->copyMetadata(&F, 0);
  Impl->setComdat(nullptr);

  SmallVector<AttributeSet, 16> ParamAttrs;
  collectParamAttrsWithTable(F, ParamAttrs);
  Impl->setAttributes(AttributeList::get(Ctx, F.getAttributes().getFnAttrs(),
                                         F.getAttributes().getRetAttrs(),
                                         ParamAttrs));
  removeVLLVMAttributes(*Impl);

  Impl->splice(Impl->begin(), &F);

  auto ImplArg = Impl->arg_begin();
  for (Argument &OldArg : F.args()) {
    OldArg.replaceAllUsesWith(&*ImplArg);
    ImplArg->takeName(&OldArg);
    ++ImplArg;
  }
  ImplArg->setName("vllvm.const.table");
  ConstTable.setTableBase(&*ImplArg);
  ConstTable.rebaseUsesInFunction(*Impl);

  BasicBlock *Entry = BasicBlock::Create(Ctx, "entry", &F);
  IRBuilder<> IRB(Entry);
  SmallVector<Value *, 16> Args;
  Args.reserve(F.arg_size() + 1);
  for (Argument &Arg : F.args())
    Args.push_back(&Arg);
  Args.push_back(ConstTable.getGlobal());

  CallInst *Call = IRB.CreateCall(Impl, Args);
  Call->setCallingConv(Impl->getCallingConv());
  Call->setAttributes(AttributeList::get(Ctx, AttributeSet(),
                                         F.getAttributes().getRetAttrs(),
                                         ParamAttrs));

  if (OldTy->getReturnType()->isVoidTy())
    IRB.CreateRetVoid();
  else
    IRB.CreateRet(Call);

  removeVLLVMAttributes(F);

  return Impl;
}

FlattenPlan planFlatten(Function &F, FunctionAnalysisManager &FAM,
                        CryptoUtils &Crypto, SharedConstTable &ConstTable) {
  FlattenPlan Plan;
  if (F.empty() || F.isDeclaration())
    return Plan;

  LowerSwitchPass LowerSwitch;
  LowerSwitch.run(F, FAM);

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
  } else if (isa<InvokeInst>(EntryBB->getTerminator())) {
    // entry 自身要变成 dispatcher 初始化块；如果原入口以 invoke 结束，
    // 先把 invoke 切到首个 case 中，unwind 边仍由 invoke 自己维护。
    BasicBlock::iterator I = EntryBB->getTerminator()->getIterator();
    BasicBlock *TmpBB = EntryBB->splitBasicBlock(I, "first");
    Plan.FlattenBBs.insert(Plan.FlattenBBs.begin(), TmpBB);
  }

  for (BasicBlock &BB : F) {
    if (&BB == EntryBB || !isFlattenableBlock(BB))
      continue;
    if (std::find(Plan.FlattenBBs.begin(), Plan.FlattenBBs.end(), &BB) ==
        Plan.FlattenBBs.end())
      Plan.FlattenBBs.push_back(&BB);
  }

  if (Plan.FlattenBBs.empty())
    return Plan;

  BasicBlock *InitialStateBB = Plan.FlattenBBs.front();
  if (auto *EntryBranch = dyn_cast<BranchInst>(EntryBB->getTerminator())) {
    if (EntryBranch->isUnconditional() &&
        findStateIndexForBB(Plan, EntryBranch->getSuccessor(0))) {
      InitialStateBB = EntryBranch->getSuccessor(0);
    }
  }
  std::default_random_engine ShuffleEngine(Crypto.getRandom32());
  std::shuffle(Plan.FlattenBBs.begin(), Plan.FlattenBBs.end(), ShuffleEngine);
  std::optional<unsigned> InitialIndex =
      findStateIndexForBB(Plan, InitialStateBB);
  if (!InitialIndex)
    return FlattenPlan();
  Plan.InitialStateIndex = *InitialIndex;

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
                  FlattenPlan &Plan, SharedConstTable &ConstTable,
                  CryptoUtils &Crypto) {
  if (!Plan.Enabled)
    return false;

  LLVMContext &Ctx = F.getContext();
  Type *Int32Ty = Type::getInt32Ty(Ctx);
  BasicBlock *EntryBB = &F.getEntryBlock();

  Value *SwitchVar = ensureFlattenRuntimeIndex(F, Plan, Int32Ty);

  BasicBlock *SwitchLoopEntry =
      BasicBlock::Create(Ctx, "switchLoopEntry", &F, EntryBB);
  BasicBlock *SwitchDispatchEntry =
      BasicBlock::Create(Ctx, "switchDispatchEntry", &F, EntryBB);
  BasicBlock *SwitchLoopEnd =
      BasicBlock::Create(Ctx, "switchLoopEnd", &F, EntryBB);

  EntryBB->getTerminator()->eraseFromParent();
  EntryBB->moveBefore(SwitchLoopEntry);
  BranchInst::Create(SwitchLoopEntry, EntryBB);
  BranchInst::Create(SwitchLoopEntry, SwitchLoopEnd);
  BasicBlock *SwDefault =
      BasicBlock::Create(Ctx, "switchDefault", &F, SwitchLoopEnd);
  BranchInst::Create(SwitchLoopEnd, SwDefault);

  IRBuilder<> IRB(Ctx);
  IRB.SetInsertPoint(SwitchLoopEntry);
  Value *StateTableIndex =
      loadFlattenRuntimeIndex(IRB, SwitchVar, Int32Ty, "loadSwitchIndex");
  Value *StateOrdinal = IRB.CreateSub(
      StateTableIndex, ConstantInt::get(Int32Ty, Plan.FirstCaseConstIndex),
      "switchOrdinal");
  Value *IsValidIndex = IRB.CreateICmpULT(
      StateOrdinal, ConstantInt::get(Int32Ty, Plan.FlattenBBs.size()),
      "switchIndexInRange");
  IRB.CreateCondBr(IsValidIndex, SwitchDispatchEntry, SwDefault);

  IRB.SetInsertPoint(SwitchDispatchEntry);
  Value *CaseValue = ConstTable.load(IRB, StateTableIndex, "loadCaseValue");
  // 从 dispatcher 进入随机路由树，而不是顺序扫描每一个 case。
  BasicBlock *DispatchRoot = buildDispatchTree(
      F, SwDefault, StateTableIndex, CaseValue, Int32Ty, Plan, ConstTable, 0,
      Plan.FlattenBBs.size(), SwDefault, Crypto);
  IRB.CreateBr(DispatchRoot);

  for (BasicBlock *BB : Plan.FlattenBBs)
    BB->moveBefore(SwitchLoopEnd);

  for (unsigned BBNo = 0; BBNo < Plan.FlattenBBs.size(); ++BBNo) {
    BasicBlock *BB = Plan.FlattenBBs[BBNo];
    unsigned CurrentTableIndex = Plan.CaseConstIndexes[BBNo];
    Instruction *Term = BB->getTerminator();
    if (Term->getNumSuccessors() == 0)
      continue;

    if (auto *II = dyn_cast<InvokeInst>(Term)) {
      BasicBlock *NormalDest = II->getNormalDest();
      std::optional<unsigned> NormalIndex = findStateIndexForBB(Plan, NormalDest);
      if (!NormalIndex)
        continue;

      // invoke 的 unwind 边必须保留给 EH CFG；normal 边落到跳板块，
      // 在真正回到 dispatcher 前更新状态，invoke 返回值也能在这里被使用。
      BasicBlock *StateBB = createFlattenStateBlock(
          F, SwitchLoopEnd, SwitchVar, Int32Ty,
          CurrentTableIndex,
          Plan.CaseConstIndexes[*NormalIndex], SwitchLoopEntry,
          "invoke.set.state");
      updatePhiIncomingBlock(NormalDest, BB, StateBB);
      II->setNormalDest(StateBB);
      continue;
    }

    if (Term->getNumSuccessors() > 1 && isa<IndirectBrInst>(Term))
      continue;

    IRBuilder<> BBIRB(Term);
    if (Term->getNumSuccessors() == 1) {
      BasicBlock *SuccBB = Term->getSuccessor(0);
      std::optional<unsigned> SuccIndex = findStateIndexForBB(Plan, SuccBB);
      if (!SuccIndex)
        continue;

      addFlattenStateDelta(BBIRB, SwitchVar, Int32Ty, CurrentTableIndex,
                           Plan.CaseConstIndexes[*SuccIndex]);
      BBIRB.CreateBr(SwitchLoopEntry);
      Term->eraseFromParent();
      continue;
    }

    if (Term->getNumSuccessors() == 2) {
      auto *Br = dyn_cast<BranchInst>(Term);
      if (!Br || !Br->isConditional())
        continue;

      BasicBlock *TrueSucc = Term->getSuccessor(0);
      BasicBlock *FalseSucc = Term->getSuccessor(1);
      std::optional<unsigned> TrueIndex = findStateIndexForBB(Plan, TrueSucc);
      std::optional<unsigned> FalseIndex = findStateIndexForBB(Plan, FalseSucc);
      if (!TrueIndex && !FalseIndex)
        continue;

      if (!TrueIndex || !FalseIndex) {
        unsigned TrueTableIndex =
            TrueIndex ? Plan.CaseConstIndexes[*TrueIndex] : 0;
        unsigned FalseTableIndex =
            FalseIndex ? Plan.CaseConstIndexes[*FalseIndex] : 0;
        BasicBlock *TrueTarget =
            TrueIndex ? createFlattenStateBlock(F, SwitchLoopEnd, SwitchVar,
                                                Int32Ty, CurrentTableIndex,
                                                TrueTableIndex, SwitchLoopEntry,
                                                "branch.set.true")
                      : TrueSucc;
        BasicBlock *FalseTarget =
            FalseIndex ? createFlattenStateBlock(F, SwitchLoopEnd, SwitchVar,
                                                 Int32Ty, CurrentTableIndex,
                                                 FalseTableIndex,
                                                 SwitchLoopEntry,
                                                 "branch.set.false")
                       : FalseSucc;
        BBIRB.CreateCondBr(Br->getCondition(), TrueTarget, FalseTarget);
        Term->eraseFromParent();
        continue;
      }

      Value *StateDelta = BBIRB.CreateSelect(
          Br->getCondition(),
          ConstantInt::get(
              Int32Ty,
              makeTableIndexDelta(CurrentTableIndex,
                                  Plan.CaseConstIndexes[*TrueIndex])),
          ConstantInt::get(
              Int32Ty,
              makeTableIndexDelta(CurrentTableIndex,
                                  Plan.CaseConstIndexes[*FalseIndex])),
          "stateDelta");
      addFlattenStateDelta(BBIRB, SwitchVar, Int32Ty, StateDelta);
      BBIRB.CreateBr(SwitchLoopEntry);
      Term->eraseFromParent();
      continue;
    }
  }

  fixStackForFlatten(&F);
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
    Plan.CallSiteConstIndexes[CallSite] = ConstTable.add(EncryptedIndex);
    Plan.CallSiteKeyConstIndexes[CallSite] = ConstTable.add(Key);
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

  return new GlobalVariable(*M, FuncTableTy, true, GlobalValue::PrivateLinkage,
                            ConstantArray::get(FuncTableTy, FuncPtrs),
                            (Twine("func_table") + F.getName()).str());
}

bool applyIndirectCalls(Function &F, const IndirectCallPlan &Plan,
                        SharedConstTable &ConstTable,
                        const FlattenPlan *FPlan) {
  if (!Plan.Enabled)
    return false;

  GlobalVariable *FuncTableGV = createFuncTable(F, Plan);
  if (!FuncTableGV)
    return false;

  LLVMContext &Ctx = F.getContext();
  auto *FuncTableTy = cast<ArrayType>(FuncTableGV->getValueType());

  for (CallInst *Call : Plan.CallSites) {
    IRBuilder<> IRB(Call);
    Value *EncryptedIndex = loadConstViaFlattenIndex(
        IRB, ConstTable, FPlan, Plan.CallSiteConstIndexes.lookup(Call),
        "vllvm.icall.enc_index");
    Value *IndexKey = loadConstViaFlattenIndex(
        IRB, ConstTable, FPlan, Plan.CallSiteKeyConstIndexes.lookup(Call),
        "vllvm.icall.index_key");
    Value *Index =
        IRB.CreateXor(EncryptedIndex, IndexKey, "vllvm.icall.index");
    Value *FuncPtr = IRB.CreateInBoundsGEP(
        FuncTableTy, FuncTableGV,
        {ConstantInt::get(Type::getInt32Ty(Ctx), 0), Index});
    Value *FuncAddr =
        IRB.CreateLoad(IRB.getPtrTy(), FuncPtr, "vllvm.icall.func");
    Call->setCalledOperand(FuncAddr);
  }

  return true;
}

LocalVarPlan planLocalVars(Function &F, SharedConstTable &ConstTable) {
  LocalVarPlan Plan;
  if (F.empty() || F.isDeclaration() || F.hasFnAttribute(Attribute::Naked) ||
      hasUnsupportedExit(F))
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
      M->createRNG((Twine("vllvm.vmfla.localvars.") + F.getName()).str());
  for (unsigned SlotNo = 0; SlotNo < Plan.Slots.size(); ++SlotNo) {
    LocalSlot &Slot = Plan.Slots[SlotNo];
    uint32_t OffsetKey = makeNonZeroKey(*RNG, SlotNo);
    uint32_t EncryptedOffset =
        static_cast<uint32_t>(Layout->getElementOffset(Slot.FieldIndex)) ^
        OffsetKey;

    Slot.ConstIndex = ConstTable.add(EncryptedOffset);
    Slot.KeyConstIndex = ConstTable.add(OffsetKey);
  }

  Plan.Enabled = true;
  return Plan;
}

bool applyLocalVars(Function &F, const LocalVarPlan &Plan,
                    SharedConstTable &ConstTable,
                    FlattenPlan *FPlan = nullptr) {
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
      IntPtrTy, Plan.StructTy, AllocSize, nullptr, nullptr, "vllvm.locals.raw");
  Value *StructPtr = RawStructPtr;
  if (Plan.MaxAlign.value() > 1) {
    Value *RawInt =
        FirstIRB.CreatePtrToInt(RawStructPtr, IntPtrTy, "vllvm.locals.int");
    Value *Biased =
        FirstIRB.CreateAdd(RawInt, getIntPtrConstant(IntPtrTy, ExtraAlignBytes),
                           "vllvm.locals.bias");
    Constant *Mask = ConstantInt::get(
        IntPtrTy, APInt(PtrBits, ~(Plan.MaxAlign.value() - 1)));
    Value *AlignedInt =
        FirstIRB.CreateAnd(Biased, Mask, "vllvm.locals.aligned_int");
    StructPtr = FirstIRB.CreateIntToPtr(AlignedInt, FirstIRB.getPtrTy(),
                                        "vllvm.locals");
  }

  // runtime index 是其他表访问的根。它本身搬进结构体时用直接 GEP 定位，
  // 避免“先读取 index 才能算出 index 地址”的自引用。
  const LocalSlot *RuntimeIndexSlot = nullptr;
  if (FPlan && FPlan->RuntimeIndexAlloca) {
    for (const LocalSlot &Slot : Plan.Slots) {
      if (Slot.Alloca == FPlan->RuntimeIndexAlloca) {
        RuntimeIndexSlot = &Slot;
        break;
      }
    }
  }
  if (RuntimeIndexSlot) {
    FPlan->RuntimeIndexPtr = FirstIRB.CreateStructGEP(
        Plan.StructTy, StructPtr, RuntimeIndexSlot->FieldIndex,
        "vllvm.switch.index.slot");
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
      if (FPlan && Slot.Alloca == FPlan->RuntimeIndexAlloca &&
          FPlan->RuntimeIndexPtr) {
        U->set(FPlan->RuntimeIndexPtr);
        continue;
      }

      Instruction *InsertPt = getInsertionPointForUse(*U);
      IRBuilder<> UseIRB(InsertPt);
      Value *EncryptedOffset = loadConstViaFlattenIndex(
          UseIRB, ConstTable, FPlan, Slot.ConstIndex,
          "vllvm.local.enc_offset");
      Value *OffsetKey = loadConstViaFlattenIndex(
          UseIRB, ConstTable, FPlan, Slot.KeyConstIndex,
          "vllvm.local.offset_key");
      Value *Offset32 =
          UseIRB.CreateXor(EncryptedOffset, OffsetKey, "vllvm.local.offset32");
      Value *Offset =
          UseIRB.CreateZExtOrBitCast(Offset32, IntPtrTy, "vllvm.local.offset");
      Value *SlotPtr =
          UseIRB.CreateGEP(FirstIRB.getInt8Ty(), StructPtr, Offset, SlotName);
      U->set(SlotPtr);
    }
  }

  for (const LocalSlot &Slot : Plan.Slots)
    Slot.Alloca->eraseFromParent();

  insertFreeOnFunctionExits(F, RawStructPtr);

  return true;
}
} // namespace

PreservedAnalyses VMFlattenFuncPass::run(Function &F,
                                         FunctionAnalysisManager &FAM) {
  bool Changed = runVMFlattenFunc(F, FAM);
  return Changed ? PreservedAnalyses::none() : PreservedAnalyses::all();
}

bool VMFlattenFuncPass::runVMFlattenFunc(Function &F,
                                         FunctionAnalysisManager &FAM) {
  if (F.empty() || F.isDeclaration() || F.getFunctionType()->isVarArg())
    return false;

  SharedConstTable ConstTable(F);
  CryptoUtils Crypto(F.getParent());
  bool Changed = false;

  if (RunBogusControlFlow) {
    errs() << "[vllvm] BogusControlFlowPass:" << F.getName() << "\n";
    // 第一轮 BCF 先扩展原始 CFG，再交给 flatten/icall/lvars 继续处理。
    Changed |= applySharedBogusControlFlow(F, ConstTable, Crypto);
    if (F.hasFnAttribute("vllvm.bcf")) {
      F.removeFnAttr("vllvm.bcf");
      Changed = true;
    }
  }

  errs() << "[vllvm] VMFlattenFuncPass:" << F.getName() << "\n";

  FlattenPlan FPlan = planFlatten(F, FAM, Crypto, ConstTable);
  IndirectCallPlan IPlan = planIndirectCalls(F, Crypto, ConstTable);

  if (!FPlan.Enabled && !IPlan.Enabled) {
    LocalVarPlan LPlan = planLocalVars(F, ConstTable);
    if (!LPlan.Enabled && ConstTable.empty())
      return Changed;

    Function *Impl = moveBodyToTableParamImpl(F, ConstTable);
    if (!Impl)
      return Changed;

    Changed = true;
    if (LPlan.Enabled)
      Changed |= applyLocalVars(*Impl, LPlan, ConstTable);

    if (RunBogusControlFlow) {
      // 第二轮 BCF 作用在最终 impl 上，用来打散后续混淆重新形成的模板骨架。
      Changed |= applySharedBogusControlFlow(*Impl, ConstTable, Crypto);
    }

    shuffleFunctionBlocks(*Impl, Crypto);

    if (!ConstTable.empty())
      ConstTable.finalize();

    return Changed;
  }

  Function *Impl = moveBodyToTableParamImpl(F, ConstTable);
  if (!Impl)
    return Changed;

  Changed = true;

  if (FPlan.Enabled)
    ensureFlattenRuntimeIndex(*Impl, FPlan,
                              Type::getInt32Ty(Impl->getContext()));

  if (IPlan.Enabled)
    Changed |= applyIndirectCalls(*Impl, IPlan, ConstTable,
                                  FPlan.Enabled ? &FPlan : nullptr);

  LocalVarPlan LPlan = planLocalVars(*Impl, ConstTable);
  if (LPlan.Enabled)
    Changed |= applyLocalVars(*Impl, LPlan, ConstTable,
                              FPlan.Enabled ? &FPlan : nullptr);

  if (FPlan.Enabled)
    Changed |= applyFlatten(*Impl, FAM, FPlan, ConstTable, Crypto);

  if (RunBogusControlFlow) {
    // 第二轮 BCF 作用在最终 impl 上，用来打散 flatten 后重新形成的模板骨架。
    Changed |= applySharedBogusControlFlow(*Impl, ConstTable, Crypto);
  }

  shuffleFunctionBlocks(*Impl, Crypto);

  if (!ConstTable.empty())
    ConstTable.finalize();

  return Changed;
}
