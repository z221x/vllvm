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

void removeVLLVMStringAttrs(Function &F) {
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

// 把 fla/lvars 的每函数常量表从"全局直接引用"改为"参数传递"：函数体搬到
// 带表参数的私有 impl，原函数退化为传表包装器。反编译视图里表不再是
// 固定地址全局，而是来自调用方的参数。indirectbr/blockaddress 的地址表
// 无法跨函数搬移，musttail 要求调用与外围函数参数一致，这两类整体回退。
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

  GlobalVariable *Sink = new GlobalVariable(
      *M, I8Ty, false, GlobalValue::PrivateLinkage,
      ConstantInt::get(I8Ty, 0), (Twine("vllvm.tablesink.") + F.getName()).str());

  BasicBlock *Entry = BasicBlock::Create(Ctx, "entry", &F);
  BasicBlock *PhantomBB = BasicBlock::Create(Ctx, "vllvm.phantom", &F);
  BasicBlock *DispatchBB = BasicBlock::Create(Ctx, "vllvm.dispatch", &F);

  IRBuilder<> EB(Entry);
  LoadInst *Guard = EB.CreateLoad(I8Ty, Sink, "vllvm.tablesink.guard");
  Guard->setVolatile(true);
  Value *Taken =
      EB.CreateICmpNE(Guard, ConstantInt::get(I8Ty, 0), "vllvm.tablesink.taken");
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
  removeVLLVMStringAttrs(*Impl);

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
  removeVLLVMStringAttrs(F);

  errs() << "[vllvm] MoveTablesToImplParams:" << F.getName()
         << ":tables=" << TableList.size() << "\n";
  return true;
}
