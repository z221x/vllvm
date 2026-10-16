#include "VMFlattenFuncPass.h"

#include "CryptoUtils.h"
#include "Utils.h"
#include "config/VLLVMConfig.h"
#include "llvm/ADT/SmallPtrSet.h"
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

  // 函数体搬入 impl 后，表访问需要从临时全局表改成读取 wrapper
  // 传入的表参数。
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

// lvars 的常量项不直接用固定下标取；优先从当前 flatten case
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

// vmfla 模式下不再有共享 BCF；保留的常量表访问都来自 flatten/lvars。

// 两级分桶路由：状态 ordinal 旋转分桶 + 桶内等值比较链。
// 原二叉树对状态下标整树二分，深度 log2(N)（luaV_execute 820 case 约
// 10 级），且遍历路径随 case 序列随机变化——分支预测失败是 dispatcher
// 的主要运行时开销。分桶后桶选择是 (ordinal+随机旋转)&mask 的纯运算 +
// switch，后端对小 switch（实测 <=8 项）会退化成比较链而非跳转表，
// 因此桶数下限取 16 并按 ordinal 轮转分配保证 100% 密度，N>=16 时必然
// 生成跳转表（单个间接跳转，热点转移可被 BTB 学习）；桶均 ~4 个成员的
// 等值链期望只走 2~3 级比较。混淆形态不弱化：旋转量与桶映射逐构建随机，
// 桶内成员匹配仍是加密值比较，状态与块的映射依旧不可静态还原。
BasicBlock *buildBucketDispatch(Function &F, BasicBlock *InsertBefore,
                                Value *CaseValue, Value *StateOrdinal,
                                Type *Int32Ty, const FlattenPlan &Plan,
                                SharedConstTable &ConstTable,
                                BasicBlock *DefaultBB, CryptoUtils &Crypto,
                                SmallPtrSetImpl<BasicBlock *> &Generated) {
  LLVMContext &Ctx = F.getContext();
  unsigned CaseCount = Plan.FlattenBBs.size();
  // 桶号旋转量：逐构建随机，运行时与 switch case 值保持同一双射。
  unsigned Rotation = Crypto.getRandom32();

  // 桶数取 >= CaseCount/4 的最小 2 的幂；N>=16 时下限 16，保证后端
  // 走跳转表而不是比较链。
  unsigned BucketCount = 4;
  while (BucketCount < CaseCount / 4 && BucketCount < 4096)
    BucketCount <<= 1;
  if (CaseCount >= 16)
    BucketCount = std::max(BucketCount, 16u);
  unsigned BucketMask = BucketCount - 1;

  // 桶归属按 ordinal 轮转（i % BucketCount）：N>=BucketCount 时每个桶
  // 都非空，switch 全密度。
  SmallVector<SmallVector<unsigned, 4>> Buckets(BucketCount);
  for (unsigned I = 0; I < CaseCount; ++I)
    Buckets[I % BucketCount].push_back(I);

  SmallVector<BasicBlock *, 16> BucketHeads(BucketCount, nullptr);
  for (unsigned B = 0; B < BucketCount; ++B) {
    if (Buckets[B].empty())
      continue;

    // 桶内成员顺序打乱，链头不集中在低 ordinal。
    std::shuffle(Buckets[B].begin(), Buckets[B].end(),
                 std::default_random_engine(Crypto.getRandom32()));

    // 逆序生成比较链：每块匹配则进 case，否则落到下一成员，链尾回
    // DefaultBB。
    BasicBlock *NextTarget = DefaultBB;
    for (unsigned K = Buckets[B].size(); K-- > 0;) {
      unsigned CaseNo = Buckets[B][K];
      BasicBlock *Chain = BasicBlock::Create(Ctx, "bucketChain", &F,
                                             InsertBefore);
      Generated.insert(Chain);
      IRBuilder<> CB(Chain);
      Value *MemberConst = ConstTable.load(
          CB, Plan.CaseConstIndexes[CaseNo], "loadMemberConst");
      Value *Match = CB.CreateICmpEQ(CaseValue, MemberConst, "bucketMatch");
      CB.CreateCondBr(Match, Plan.FlattenBBs[CaseNo], NextTarget);
      NextTarget = Chain;
    }
    BucketHeads[B] = NextTarget;
  }

  BasicBlock *Head =
      BasicBlock::Create(Ctx, "bucketDispatch", &F, InsertBefore);
  Generated.insert(Head);
  IRBuilder<> IRB(Head);
  auto *I32Ty = cast<IntegerType>(Int32Ty);
  // 桶号 = (ordinal + 随机旋转) & mask：模 2^k 上的双射，桶序与 ordinal
  // 序脱钩。
  Value *Rotated = IRB.CreateAdd(
      StateOrdinal, ConstantInt::get(I32Ty, Rotation), "bucketRotate");
  Value *BucketIdx =
      IRB.CreateAnd(Rotated, ConstantInt::get(I32Ty, BucketMask), "bucketIdx");
  SwitchInst *Sw = IRB.CreateSwitch(BucketIdx, DefaultBB, BucketCount);
  for (unsigned B = 0; B < BucketCount; ++B)
    if (BucketHeads[B])
      Sw->addCase(
          ConstantInt::get(I32Ty, (B + Rotation) & BucketMask),
          BucketHeads[B]);
  return Head;
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
  F.removeFnAttr("vllvm.vmp");
  F.removeFnAttr("vllvm.bb2func");
  F.removeFnAttr("vllvm.merge");
}

// vmfla 链路的延迟标记：由调度层打在原函数上；复制属性到 impl 时必须
// 剥掉，否则 phase 2 会对新创建的 impl 再次触发 vmfla，产生多层嵌套。
void removeVLLVMPendingAttrs(Function &F) {
  F.removeFnAttr("vllvm.vmfla.pending");
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

// 防传播伪调用点：impl 若只有一个调用点且每次传入同一个表全局，
// -O2 的过程间常量传播（IPSCCP/GlobalOpt，表现为删参并转 fastcc）会把
// 表参数折叠回全局引用，参数化成果被打回原形。这里用 volatile 守卫
// （初值 0，运行时恒假，编译期不可证）构造一个动态不可达的第二调用点，
// 传入偏移后的表指针，使表参数在调用点取值不唯一，阻断传播。
void buildGuardedImplCall(Function &F, Function &Impl,
                          ArrayRef<Value *> Args,
                          ArrayRef<GlobalVariable *> Tables) {
  Module *M = F.getParent();
  LLVMContext &Ctx = F.getContext();
  Type *I8Ty = Type::getInt8Ty(Ctx);
  Type *I64Ty = Type::getInt64Ty(Ctx);

  GlobalVariable *Sink =
      new GlobalVariable(*M, I8Ty, false, GlobalValue::PrivateLinkage,
                         ConstantInt::get(I8Ty, 0),
                         (Twine("vllvm.tablesink.") + F.getName()).str());

  BasicBlock *Entry = BasicBlock::Create(Ctx, "entry", &F);
  BasicBlock *PhantomBB = BasicBlock::Create(Ctx, "vllvm.phantom", &F);
  BasicBlock *DispatchBB = BasicBlock::Create(Ctx, "vllvm.dispatch", &F);

  IRBuilder<> EB(Entry);
  LoadInst *Guard = EB.CreateLoad(I8Ty, Sink, "vllvm.tablesink.guard");
  Guard->setVolatile(true);
  Value *Taken = EB.CreateICmpNE(Guard, ConstantInt::get(I8Ty, 0),
                                 "vllvm.tablesink.taken");
  EB.CreateCondBr(Taken, PhantomBB, DispatchBB);

  IRBuilder<> PB(PhantomBB);
  SmallVector<Value *, 16> PhantomArgs(Args.begin(), Args.end());
  for (size_t I = 0, E = Tables.size(); I != E; ++I) {
    // 伪调用点的表指针整体偏移，与真实调用点取值必然不同。
    PhantomArgs[Args.size() - E + I] =
        PB.CreateGEP(I8Ty, Tables[I], ConstantInt::get(I64Ty, I + 1),
                     "vllvm.table.shifted");
  }
  CallInst *Phantom = PB.CreateCall(&Impl, PhantomArgs);
  Phantom->setCallingConv(Impl.getCallingConv());
  PB.CreateBr(DispatchBB);

  IRBuilder<> DB(DispatchBB);
  CallInst *Real = DB.CreateCall(&Impl, Args);
  Real->setCallingConv(Impl.getCallingConv());
  if (Impl.getReturnType()->isVoidTy())
    DB.CreateRetVoid();
  else
    DB.CreateRet(Real);
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
  // copyAttributesFrom 会带入外部符号的 preemptable 状态，private 实现必须本地绑定。
  Impl->setDSOLocal(true);
  Impl->copyMetadata(&F, 0);
  // 调试信息随原函数体移到 Impl；生成的包装器不能共享同一个 DISubprogram。
  F.setSubprogram(nullptr);
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

  SmallVector<Value *, 16> Args;
  Args.reserve(F.arg_size() + 1);
  for (Argument &Arg : F.args())
    Args.push_back(&Arg);
  Args.push_back(ConstTable.getGlobal());

  // 直连调用点（伪调用点方案已移除）。取舍：单调用点固定传入表全局时，
  // -O2 的 IPSCCP/GlobalOpt 可能把 impl 的表参数折叠回全局引用（删参），
  // 表参数化的反编译形态随之弱化；语义不受影响。
  BasicBlock *Entry = BasicBlock::Create(Ctx, "entry", &F);
  IRBuilder<> EB(Entry);
  CallInst *Call = EB.CreateCall(Impl, Args);
  Call->setCallingConv(Impl->getCallingConv());
  if (Impl->getReturnType()->isVoidTy())
    EB.CreateRetVoid();
  else
    EB.CreateRet(Call);

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
  // 调度器自身生成块的集合：区分从 dispatch 结构进入 case 的合法边与
  // indirectbr/孤岛块的非常规直连边。
  SmallPtrSet<BasicBlock *, 32> DispatchBlocks;
  DispatchBlocks.insert(SwitchLoopEntry);
  DispatchBlocks.insert(SwitchDispatchEntry);
  DispatchBlocks.insert(SwitchLoopEnd);
  DispatchBlocks.insert(SwDefault);
  // 从 dispatcher 进入旋转分桶 + 桶内比较链的路由结构。
  BasicBlock *DispatchRoot =
      buildBucketDispatch(F, SwDefault, CaseValue, StateOrdinal, Int32Ty,
                          Plan, ConstTable, SwDefault, Crypto, DispatchBlocks);
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

  // 状态更新是"当前下标 + 常量增量"的相对编码，依赖不变式"进入 case 时
  // 运行时下标 == 该 case 的表下标"。indirectbr（computed goto，如解释器
  // dispatch 表）与孤岛块到 case 的直连边绕过 dispatcher，运行时下标仍是
  // 旧值，增量算出错误状态：dispatcher 按 case 值匹配失败落入死循环，或
  // 恰好命中错误 case。在仍有非常规前驱的 case 头部写回自身下标重建
  // 不变式；lvars 的 loadConstViaFlattenIndex 锚定读取同样依赖它。
  for (unsigned BBNo = 0; BBNo < Plan.FlattenBBs.size(); ++BBNo) {
    BasicBlock *BB = Plan.FlattenBBs[BBNo];
    bool HasIrregularPred = false;
    for (BasicBlock *Pred : predecessors(BB)) {
      if (!DispatchBlocks.count(Pred)) {
        HasIrregularPred = true;
        break;
      }
    }
    if (!HasIrregularPred)
      continue;

    IRBuilder<> HeadIRB(BB, BB->getFirstInsertionPt());
    storeFlattenRuntimeIndex(
        HeadIRB, SwitchVar,
        ConstantInt::get(Int32Ty, Plan.CaseConstIndexes[BBNo]));
  }

  fixStackForFlatten(&F);
  // 注意：此处不能再跑 LowerSwitch——用户的 switch 已在 planFlatten 入口
  // 统一降级，再跑会把分桶路由的 switch 展开回比较树，跳转表失效。
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

  // 布局前随机打乱 alloca 顺序：字段偏移不再对应源码声明顺序，反编译
  // 时无法按偏移相邻关系推断变量的原始排列。
  std::unique_ptr<RandomNumberGenerator> ShuffleRNG = M->createRNG(
      (Twine("vllvm.vmfla.localvars.shuffle.") + F.getName()).str());
  std::shuffle(Allocas.begin(), Allocas.end(), *ShuffleRNG);

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

// 把每函数常量表（fla 路由表等）改为参数传递：函数体搬到带表参数的
// 私有 impl，原函数退化为传表包装器。反编译视图里表不再是固定地址
// 全局，而是来自调用方的参数。indirectbr/blockaddress 的地址表无法
// 跨函数搬移，musttail 要求调用与外围函数参数一致，两类场景整体回退。
bool moveTablesToImplParams(Function &F) {
  if (F.empty() || F.isDeclaration() || F.getFunctionType()->isVarArg())
    return false;

  SmallPtrSet<GlobalVariable *, 4> Tables;
  // 宽松收集：操作数上出现每函数常量表即命中。全常量下标的 GEP 已被
  // IRBuilder 折叠成 ConstantExpr，仅靠 GEP 指令扫描会漏掉 lvars 表。
  auto CollectLoose = [&]() {
    Tables.clear();
    for (BasicBlock &BB : F) {
      if (isa<IndirectBrInst>(BB.getTerminator()))
        return false;
      for (Instruction &I : BB) {
        if (auto *CB = dyn_cast<CallBase>(&I))
          if (CB->isMustTailCall())
            return false;
        for (Value *Op : I.operands()) {
          if (isa<BlockAddress>(Op))
            return false;
          if (auto *GV = dyn_cast<GlobalVariable>(Op->stripPointerCasts()))
            if (GV->getName().starts_with("vllvm.") &&
                GV->getName().contains(".table."))
              Tables.insert(GV);
        }
      }
    }
    return true;
  };
  // 严格收集：只认 GEP 指令直接引用的表。仅作为 call 实参出现的表
  // （如 vmfla 包装器向 impl 传表）不会被参数化，避免二次包装。
  auto CollectStrict = [&]() {
    Tables.clear();
    for (BasicBlock &BB : F)
      for (Instruction &I : BB) {
        auto *GEP = dyn_cast<GetElementPtrInst>(&I);
        if (!GEP)
          continue;
        if (auto *GV = dyn_cast<GlobalVariable>(
                GEP->getPointerOperand()->stripPointerCasts()))
          if (GV->getName().starts_with("vllvm.") &&
              GV->getName().contains(".table."))
            Tables.insert(GV);
      }
    return !Tables.empty();
  };

  if (!CollectLoose() || Tables.empty())
    return false;

  // 把折叠的 ConstantExpr 展开成指令，表的引用才能统一改写成参数。
  expandConstantExpr(F);
  if (!CollectStrict())
    return false;

  // 展开会遗留 use_empty 的孤儿常量表达式，它们仍持有表的引用；就地
  // 销毁并重扫，直到表只剩指令用户。带真实用户的常量链无法安全改写，
  // 整体回退。
  while (true) {
    bool Changed = false;
    for (GlobalVariable *GV : Tables)
      for (User *U : GV->users()) {
        if (isa<Instruction>(U))
          continue;
        if (U->use_empty()) {
          cast<Constant>(U)->destroyConstant();
          Changed = true;
          break;
        }
        return false;
      }
    if (!Changed)
      break;
  }

  Module *M = F.getParent();
  LLVMContext &Ctx = F.getContext();
  FunctionType *OldTy = F.getFunctionType();

  SmallVector<Type *, 16> Params(OldTy->param_begin(), OldTy->param_end());
  Params.append(Tables.size(), PointerType::get(Ctx, 0));
  FunctionType *ImplTy =
      FunctionType::get(OldTy->getReturnType(), Params, false);
  Function *Impl =
      Function::Create(ImplTy, GlobalValue::PrivateLinkage,
                       F.getAddressSpace(), F.getName() + ".vllvm.impl", M);
  Impl->copyAttributesFrom(&F);
  Impl->setDSOLocal(true);
  Impl->copyMetadata(&F, 0);
  // 调试信息随函数体转移到 impl；包装器不能共享同一个 DISubprogram。
  F.setSubprogram(nullptr);
  Impl->setComdat(nullptr);

  SmallVector<AttributeSet, 16> ParamAttrs;
  for (unsigned I = 0, E = F.arg_size(); I != E; ++I)
    ParamAttrs.push_back(F.getAttributes().getParamAttrs(I));
  ParamAttrs.append(Tables.size(), AttributeSet());
  Impl->setAttributes(AttributeList::get(Ctx, F.getAttributes().getFnAttrs(),
                                         F.getAttributes().getRetAttrs(),
                                         ParamAttrs));
  removeVLLVMAttributes(*Impl);
  // F 在 vmfla 链路里已被调度层打上 pending 标记；impl 复制属性时不能
  // 继承，否则 phase 2 会对 impl 再次触发 vmfla，产生多层嵌套包装。
  removeVLLVMPendingAttrs(*Impl);

  Impl->splice(Impl->begin(), &F);

  auto ImplArg = Impl->arg_begin();
  for (Argument &OldArg : F.args()) {
    OldArg.replaceAllUsesWith(&*ImplArg);
    ImplArg->takeName(&OldArg);
    ++ImplArg;
  }

  // 表全局的全部引用改写为 impl 的表参数；全局本身保留，由包装器传回。
  SmallVector<GlobalVariable *, 4> TableList(Tables.begin(), Tables.end());
  llvm::sort(TableList, [](GlobalVariable *A, GlobalVariable *B) {
    return A->getName() < B->getName();
  });
  for (GlobalVariable *GV : TableList) {
    ImplArg->setName("vllvm.const.table");
    GV->replaceAllUsesWith(&*ImplArg);
    ++ImplArg;
  }

  SmallVector<Value *, 16> CallArgs;
  for (Argument &Arg : F.args())
    CallArgs.push_back(&Arg);
  for (GlobalVariable *GV : TableList)
    CallArgs.push_back(GV);
  buildGuardedImplCall(F, *Impl, CallArgs, TableList);
  // wrapper 会执行完整原函数体，原先基于旧函数体的内存/异常属性失真。
  F.removeFnAttr(Attribute::NoUnwind);
  F.removeFnAttr(Attribute::NoFree);
  F.removeFnAttr(Attribute::ReadNone);
  F.removeFnAttr(Attribute::ReadOnly);
  F.removeFnAttr(Attribute::Memory);
  // 清 vllvm 标记，防止后续 pass 再把 wrapper 当混淆目标。
  removeVLLVMAttributes(F);

  errs() << "[vllvm] MoveTablesToImplParams:" << F.getName()
         << ":tables=" << TableList.size() << "\n";
  return true;
}

PreservedAnalyses VMFlattenFuncPass::run(Function &F,
                                         FunctionAnalysisManager &FAM) {
  llvm::vllvm::VLLVMConfig &Config = llvm::vllvm::VLLVMConfig::get();
  Config.registerPassLevels("vmfla", 1, 2);
  if (!Config.isEnabled("vmfla"))
    return PreservedAnalyses::all();
  bool Changed = runVMFlattenFunc(F, FAM, Config.getLevel("vmfla"));
  return Changed ? PreservedAnalyses::none() : PreservedAnalyses::all();
}

// level 设置混淆强度：1=bb2func+merge+平坦化；2=再开启局部变量结构化。
bool VMFlattenFuncPass::runVMFlattenFunc(Function &F,
                                         FunctionAnalysisManager &FAM,
                                         unsigned Level) {
  if (F.empty() || F.isDeclaration() || F.getFunctionType()->isVarArg())
    return false;

  const bool RunLocalVars = Level >= 2;

  // 与 enstr 共用 PHI 降级，必须早于 flatten 的 CFG 改写。
  PHILoweringResult PHIs = lowerPHINodes(F);
  if (PHIs == PHILoweringResult::Unsupported)
    return false;
  SharedConstTable ConstTable(F);
  CryptoUtils Crypto(F.getParent());
  bool Changed = PHIs == PHILoweringResult::Lowered;

  errs() << "[vllvm] VMFlattenFuncPass:" << F.getName() << "\n";

  FlattenPlan FPlan = planFlatten(F, FAM, Crypto, ConstTable);

  if (!FPlan.Enabled) {
    LocalVarPlan LPlan = RunLocalVars ? planLocalVars(F, ConstTable)
                                      : LocalVarPlan();
    if (!LPlan.Enabled && ConstTable.empty())
      return Changed;

    Function *Impl = moveBodyToTableParamImpl(F, ConstTable);
    if (!Impl)
      return Changed;

    Changed = true;
    if (LPlan.Enabled)
      Changed |= applyLocalVars(*Impl, LPlan, ConstTable);

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

  LocalVarPlan LPlan = RunLocalVars ? planLocalVars(*Impl, ConstTable)
                                    : LocalVarPlan();
  if (LPlan.Enabled)
    Changed |= applyLocalVars(*Impl, LPlan, ConstTable,
                              FPlan.Enabled ? &FPlan : nullptr);

  if (FPlan.Enabled)
    Changed |= applyFlatten(*Impl, FAM, FPlan, ConstTable, Crypto);

  shuffleFunctionBlocks(*Impl, Crypto);

  if (!ConstTable.empty())
    ConstTable.finalize();

  return Changed;
}
