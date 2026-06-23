#include "BogusControlFlowPass.h"
#include "Utils.h"

#include "llvm/ADT/SmallVector.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Module.h"

#include <cstdint>
#include <utility>

using namespace llvm;

namespace {
// 独立 BCF 也使用随机覆盖率，避免每个函数都生成同构 fake 分支。
constexpr unsigned BogusMinProbability = 55;
constexpr unsigned BogusProbabilityRange = 36;
constexpr unsigned BogusLoops = 1;

bool isGeneratedBlock(BasicBlock &BB) {
  return BB.hasName() && BB.getName().starts_with("vllvm.bcf.");
}

bool hasUnsupportedEH(Function &F) {
  for (Instruction &I : instructions(F)) {
    if (isa<InvokeInst>(&I) || isa<LandingPadInst>(&I) ||
        isa<CatchPadInst>(&I) || isa<CleanupPadInst>(&I) ||
        isa<CatchSwitchInst>(&I) || isa<CatchReturnInst>(&I) ||
        isa<CleanupReturnInst>(&I) || isa<ResumeInst>(&I))
      return true;
  }
  return false;
}

bool hasMustTailReturn(BasicBlock &BB) {
  auto *RI = dyn_cast<ReturnInst>(BB.getTerminator());
  if (!RI)
    return false;

  Instruction *Prev = RI->getPrevNonDebugInstruction();
  auto *CB = dyn_cast_or_null<CallBase>(Prev);
  return CB && CB->isMustTailCall();
}

bool canSplitForBogusFlow(BasicBlock &BB) {
  Instruction *Term = BB.getTerminator();
  if (!Term || isGeneratedBlock(BB) || BB.isEHPad() || hasMustTailReturn(BB))
    return false;

  if (isa<IndirectBrInst>(Term) || isa<CallBrInst>(Term) ||
      isa<CatchSwitchInst>(Term) || isa<CatchReturnInst>(Term) ||
      isa<CleanupReturnInst>(Term) || isa<ResumeInst>(Term))
    return false;

  return true;
}

GlobalVariable *createPredicateGlobal(Function &F, StringRef Suffix,
                                      uint32_t Value) {
  Module *M = F.getParent();
  LLVMContext &Ctx = F.getContext();
  auto *I32Ty = Type::getInt32Ty(Ctx);
  auto *GV = new GlobalVariable(
      *M, I32Ty, false, GlobalValue::PrivateLinkage,
      ConstantInt::get(I32Ty, Value),
      (Twine("vllvm.bcf.") + Suffix + "." + F.getName()).str());
  GV->setUnnamedAddr(GlobalValue::UnnamedAddr::Global);
  return GV;
}

LoadInst *createVolatileI32Load(IRBuilder<> &IRB, GlobalVariable *GV,
                                Twine Name) {
  auto *Load = IRB.CreateLoad(IRB.getInt32Ty(), GV, Name);
  Load->setVolatile(true);
  return Load;
}
} // namespace

PreservedAnalyses BogusControlFlowPass::run(Function &F,
                                            FunctionAnalysisManager &FAM) {
  errs() << "[vllvm] BogusControlFlowPass:" << F.getName() << "\n";
  bool IsChanged = runBogusControlFlow(F);
  return IsChanged ? PreservedAnalyses::none() : PreservedAnalyses::all();
}

bool BogusControlFlowPass::runBogusControlFlow(Function &F) {
  if (F.empty() || F.isDeclaration() || F.hasFnAttribute(Attribute::Naked) ||
      hasUnsupportedEH(F))
    return false;

  CryptoUtils Crypto(F.getParent());
  uint32_t XSeed = Crypto.getRandom32();
  uint32_t YSeed = Crypto.getRandom32();
  GlobalVariable *X = createPredicateGlobal(F, "x", XSeed);
  GlobalVariable *Y = createPredicateGlobal(F, "y", YSeed);

  bool Changed = false;
  for (unsigned Loop = 0; Loop < BogusLoops; ++Loop) {
    SmallVector<BasicBlock *, 32> Candidates;
    SmallVector<BasicBlock *, 32> Targets;
    unsigned Probability =
        BogusMinProbability + (Crypto.getRandom32() % BogusProbabilityRange);
    // 随机选择一部分可拆块，减少 CFG 中整齐重复的菱形结构。
    for (BasicBlock &BB : F) {
      if (!canSplitForBogusFlow(BB))
        continue;
      Candidates.push_back(&BB);
      if (Crypto.getRandom32() % 100 < Probability)
        Targets.push_back(&BB);
    }
    // 至少选择一个候选块，避免小函数显式启用 bcf 时没有变化。
    if (Targets.empty() && !Candidates.empty())
      Targets.push_back(Candidates[Crypto.getRandom32() % Candidates.size()]);

    for (BasicBlock *BB : Targets)
      if (BB->getParent() == &F)
        Changed |= addBogusFlow(*BB, X, Y, XSeed, YSeed, Crypto);
  }

  if (Changed)
    fixStack(&F);
  else {
    X->eraseFromParent();
    Y->eraseFromParent();
  }
  return Changed;
}

bool BogusControlFlowPass::addBogusFlow(BasicBlock &BB, GlobalVariable *X,
                                        GlobalVariable *Y,
                                        uint32_t XSeed, uint32_t YSeed,
                                        CryptoUtils &Crypto) {
  Instruction *Term = BB.getTerminator();
  if (!Term || !canSplitForBogusFlow(BB))
    return false;

  Function *F = BB.getParent();
  LLVMContext &Ctx = F->getContext();

  BasicBlock *Tail = BB.splitBasicBlock(Term->getIterator(), "vllvm.bcf.tail");
  BasicBlock *Fake =
      BasicBlock::Create(Ctx, "vllvm.bcf.fake", F, Tail->getNextNode());

  BB.getTerminator()->eraseFromParent();

  IRBuilder<> RealIRB(&BB);
  bool PredicateIsTrue = true;
  Value *Predicate = createOpaquePredicate(RealIRB, X, Y, XSeed, YSeed, Crypto,
                                           PredicateIsTrue);
  BasicBlock *TrueBB = PredicateIsTrue ? Tail : Fake;
  BasicBlock *FalseBB = PredicateIsTrue ? Fake : Tail;
  RealIRB.CreateCondBr(Predicate, TrueBB, FalseBB);

  terminateFakePath(Fake, Tail, X, Y, XSeed, YSeed, Crypto);
  return true;
}

Value *BogusControlFlowPass::createOpaquePredicate(IRBuilder<> &IRB,
                                                   GlobalVariable *X,
                                                   GlobalVariable *Y,
                                                   uint32_t XSeed,
                                                   uint32_t YSeed,
                                                   CryptoUtils &Crypto,
                                                   bool &PredicateIsTrue) {
  Value *XVal = createVolatileI32Load(IRB, X, "vllvm.bcf.x");
  Value *YVal = createVolatileI32Load(IRB, Y, "vllvm.bcf.y");
  Value *Expr = (Crypto.getRandom32() & 1) ? XVal : YVal;
  uint32_t Expected = Expr == XVal ? XSeed : YSeed;

  auto Other = [&](bool UseX) -> std::pair<Value *, uint32_t> {
    return UseX ? std::make_pair(XVal, XSeed) : std::make_pair(YVal, YSeed);
  };

  unsigned Rounds = 4 + (Crypto.getRandom32() % 5);
  for (unsigned I = 0; I < Rounds; ++I) {
    uint32_t K = Crypto.getRandom32();
    switch (Crypto.getRandom32() % 6) {
    case 0:
      Expr = IRB.CreateAdd(Expr, IRB.getInt32(K), "vllvm.bcf.mix.add");
      Expected += K;
      break;
    case 1:
      Expr = IRB.CreateSub(Expr, IRB.getInt32(K), "vllvm.bcf.mix.sub");
      Expected -= K;
      break;
    case 2:
      K |= 1U;
      Expr = IRB.CreateMul(Expr, IRB.getInt32(K), "vllvm.bcf.mix.mul");
      Expected *= K;
      break;
    case 3:
      Expr = IRB.CreateXor(Expr, IRB.getInt32(K), "vllvm.bcf.mix.xor");
      Expected ^= K;
      break;
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
  Value *ExpectedValue = IRB.getInt32(Expected);
  return Negate ? IRB.CreateICmpNE(Expr, ExpectedValue, "vllvm.bcf.pred")
                : IRB.CreateICmpEQ(Expr, ExpectedValue, "vllvm.bcf.pred");
}

void BogusControlFlowPass::insertBogusJunk(IRBuilder<> &IRB, GlobalVariable *X,
                                           GlobalVariable *Y,
                                           CryptoUtils &Crypto) {
  Value *A = createVolatileI32Load(IRB, X, "vllvm.bcf.fake.x");
  Value *B = createVolatileI32Load(IRB, Y, "vllvm.bcf.fake.y");

  Value *Junk = IRB.CreateXor(A, B, "vllvm.bcf.fake.mix");
  Junk = IRB.CreateAdd(Junk, IRB.getInt32(Crypto.getRandom32()),
                       "vllvm.bcf.fake.add");
  Junk = IRB.CreateMul(Junk, IRB.getInt32((Crypto.getRandom32() | 1U)),
                       "vllvm.bcf.fake.mul");

  StoreInst *Store = IRB.CreateStore(Junk, X);
  Store->setVolatile(true);
}

// fake 路径随机生成短链、分叉或自环，避免固定 fake -> tail 模板。
void BogusControlFlowPass::terminateFakePath(BasicBlock *Fake, BasicBlock *Tail,
                                             GlobalVariable *X,
                                             GlobalVariable *Y,
                                             uint32_t XSeed, uint32_t YSeed,
                                             CryptoUtils &Crypto) {
  Function *F = Fake->getParent();
  LLVMContext &Ctx = F->getContext();

  auto CreatePredicate = [&](IRBuilder<> &IRB) {
    bool PredicateIsTrue = true;
    return createOpaquePredicate(IRB, X, Y, XSeed, YSeed, Crypto,
                                 PredicateIsTrue);
  };

  switch (Crypto.getRandom32() % 4) {
  case 0: {
    IRBuilder<> FakeIRB(Fake);
    insertBogusJunk(FakeIRB, X, Y, Crypto);
    FakeIRB.CreateBr(Tail);
    return;
  }
  case 1: {
    BasicBlock *Next =
        BasicBlock::Create(Ctx, "vllvm.bcf.fake.next", F, Tail->getNextNode());
    IRBuilder<> FakeIRB(Fake);
    insertBogusJunk(FakeIRB, X, Y, Crypto);
    FakeIRB.CreateCondBr(CreatePredicate(FakeIRB), Next, Tail);

    IRBuilder<> NextIRB(Next);
    insertBogusJunk(NextIRB, X, Y, Crypto);
    NextIRB.CreateBr(Tail);
    return;
  }
  case 2: {
    BasicBlock *Left =
        BasicBlock::Create(Ctx, "vllvm.bcf.fake.left", F, Tail->getNextNode());
    BasicBlock *Right =
        BasicBlock::Create(Ctx, "vllvm.bcf.fake.right", F, Tail->getNextNode());
    IRBuilder<> FakeIRB(Fake);
    insertBogusJunk(FakeIRB, X, Y, Crypto);
    FakeIRB.CreateCondBr(CreatePredicate(FakeIRB), Left, Right);

    IRBuilder<> LeftIRB(Left);
    insertBogusJunk(LeftIRB, X, Y, Crypto);
    LeftIRB.CreateBr(Tail);

    IRBuilder<> RightIRB(Right);
    insertBogusJunk(RightIRB, X, Y, Crypto);
    RightIRB.CreateBr(Tail);
    return;
  }
  default: {
    BasicBlock *Loop =
        BasicBlock::Create(Ctx, "vllvm.bcf.fake.loop", F, Tail->getNextNode());
    IRBuilder<> FakeIRB(Fake);
    insertBogusJunk(FakeIRB, X, Y, Crypto);
    FakeIRB.CreateCondBr(CreatePredicate(FakeIRB), Loop, Tail);

    IRBuilder<> LoopIRB(Loop);
    insertBogusJunk(LoopIRB, X, Y, Crypto);
    Value *KeepLooping = CreatePredicate(LoopIRB);
    if (Crypto.getRandom32() & 1)
      LoopIRB.CreateCondBr(KeepLooping, Tail, Loop);
    else
      LoopIRB.CreateCondBr(KeepLooping, Loop, Tail);
    return;
  }
  }
}
