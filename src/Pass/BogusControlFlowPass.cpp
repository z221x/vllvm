#include "BogusControlFlowPass.h"

#include "CryptoUtils.h"
#include "config/VLLVMConfig.h"

#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Instructions.h"

#include <cstdint>

using namespace llvm;

namespace {

// pass 自产虚假块的块名前缀，也是判别生成块的依据。
constexpr llvm::StringRef BogusBlockPrefix = "vllvm.bcf.fake.";

// 等级 1/2 对单后继分支的随机改写覆盖率；等级 3 全量改写。
constexpr unsigned Level1Probability = 60;
constexpr unsigned Level2Probability = 85;

// seed 的已知运行时值：运算统一用 uint16（i16 回绕），x + (x ^ -1)
// 在 i16 上恒等于 0xFFFF。选用 add 形态：SelectionDAG 会对 xor/or 形态
// 做 known-bits 折叠，add 带进位不会被折，Hex-Rays 类反编译器同样不做
// 该代数化简。
constexpr uint32_t KnownSeedValue = 0xFFFFu;

bool isGeneratedBlock(BasicBlock &BB) {
  return BB.hasName() && BB.getName().starts_with(BogusBlockPrefix);
}

bool hasMustTailReturn(BasicBlock &BB) {
  auto *RI = dyn_cast<ReturnInst>(BB.getTerminator());
  if (!RI)
    return false;

  Instruction *Prev = RI->getPrevNonDebugInstruction();
  auto *CB = dyn_cast_or_null<CallBase>(Prev);
  return CB && CB->isMustTailCall();
}

// 可作为虚假后继的块：入口块不允许有前驱；EH pad 块只能被异常边进入；
// musttail 尾块依赖调用点契约。真实块与虚假块都在候选池内。
bool canBogusBranchTo(BasicBlock &BB) {
  return &BB != &BB.getParent()->getEntryBlock() && !BB.isEHPad() &&
         !hasMustTailReturn(BB);
}

// 新前驱进入带 PHI 的目标块时补 undef incoming；假边永不执行，取值
// 不影响语义。
void addEdgeFixPhi(BasicBlock *From, BasicBlock *To) {
  for (PHINode &PN : To->phis())
    if (PN.getBasicBlockIndex(From) < 0)
      PN.addIncoming(UndefValue::get(PN.getType()), From);
}

// seed 变量压入栈：值不能是明文常量，否则反编译器能顺着栈槽把密文链
// 折叠掉、永假谓词直接消失。这里以槽位自身地址（栈帧位置，静态不可知）
// 为不透明源，经恒等式 add(x, xor(x,-1)) 归一成已知值 -1 后 volatile
// 写入：运行时恒为 KnownSeedValue，编译期与反编译期均不可求解。
AllocaInst *createSeedSlot(Function &F, SmallVectorImpl<Instruction *> &Chain) {
  BasicBlock *Entry = &F.getEntryBlock();
  IRBuilder<> IRB(Entry->getTerminator());
  Type *I16Ty = Type::getInt16Ty(F.getContext());

  AllocaInst *Slot = IRB.CreateAlloca(I16Ty, 0, "vllvm.bcf.seed");
  Value *Addr = IRB.CreatePtrToInt(Slot, Type::getInt64Ty(F.getContext()),
                                   "vllvm.bcf.seed.addr");
  Value *X = IRB.CreateTrunc(Addr, I16Ty, "vllvm.bcf.seed.x");
  Value *NotX = IRB.CreateXor(X, ConstantInt::get(I16Ty, -1),
                              "vllvm.bcf.seed.not");
  Value *Norm = IRB.CreateAdd(X, NotX, "vllvm.bcf.seed.norm");
  StoreInst *Store = IRB.CreateStore(Norm, Slot, "vllvm.bcf.seed.init");
  Store->setVolatile(true);

  for (Value *V : {cast<Value>(Slot), Addr, X, NotX, Norm, cast<Value>(Store)})
    Chain.push_back(cast<Instruction>(V));
  return Slot;
}

LoadInst *loadSeedVolatile(IRBuilder<> &IRB, AllocaInst *SeedSlot) {
  LoadInst *Load = IRB.CreateLoad(Type::getInt16Ty(IRB.getContext()), SeedSlot,
                                  "vllvm.bcf.seed.load");
  Load->setVolatile(true);
  return Load;
}

// 随机算法运算加密：对 seed 施加随机 add/sub/xor/mul 链。指令与镜像
// 计算同步进行，加密后的值在编译期已知（Expected）。
// 全部运算在 uint16（i16 回绕）上进行；镜像计算同步掩码，加密后的值
// 在编译期已知。
Value *buildEncryptionChain(IRBuilder<> &IRB, Value *Seed, uint32_t SeedValue,
                            CryptoUtils &Crypto, uint32_t &Encrypted) {
  Type *I16Ty = Type::getInt16Ty(IRB.getContext());
  uint32_t Expected = SeedValue & 0xFFFF;
  Value *V = Seed;

  unsigned Rounds = 3 + Crypto.getRandom32() % 4;
  for (unsigned I = 0; I < Rounds; ++I) {
    uint32_t K = Crypto.getRandom32() & 0xFFFF;
    switch (Crypto.getRandom32() % 4) {
    case 0:
      V = IRB.CreateAdd(V, ConstantInt::get(I16Ty, K), "vllvm.bcf.enc.add");
      Expected = (Expected + K) & 0xFFFF;
      break;
    case 1:
      V = IRB.CreateSub(V, ConstantInt::get(I16Ty, K), "vllvm.bcf.enc.sub");
      Expected = (Expected - K) & 0xFFFF;
      break;
    case 2:
      V = IRB.CreateXor(V, ConstantInt::get(I16Ty, K), "vllvm.bcf.enc.xor");
      Expected ^= K;
      break;
    default:
      V = IRB.CreateMul(V, ConstantInt::get(I16Ty, K), "vllvm.bcf.enc.mul");
      Expected = (Expected * K) & 0xFFFF;
      break;
    }
  }

  Encrypted = Expected;
  return V;
}

// 生成随机指令的虚假块：volatile 读 seed、随机运算链、volatile 写回。
// 不设终结指令，由调用方把虚假块链接成链并锚定到永假分支后继上，
// 否则不可达块不会进入后端发射，反编译时直接消失。
BasicBlock *createJunkBlockBody(Function &F, AllocaInst *SeedSlot,
                                CryptoUtils &Crypto, unsigned Index) {
  BasicBlock *BB = BasicBlock::Create(
      F.getContext(), (BogusBlockPrefix + Twine(Index)).str(), &F);
  IRBuilder<> IRB(BB);

  Value *V = loadSeedVolatile(IRB, SeedSlot);
  uint32_t Discarded;
  V = buildEncryptionChain(IRB, V, KnownSeedValue, Crypto, Discarded);
  StoreInst *Store = IRB.CreateStore(V, SeedSlot, "vllvm.bcf.junk.store");
  Store->setVolatile(true);
  return BB;
}

void eraseSeedChain(MutableArrayRef<Instruction *> Chain) {
  for (Instruction *I : llvm::reverse(Chain))
    I->eraseFromParent();
}

bool runBogusControlFlow(Function &F, unsigned Level, CryptoUtils &Crypto) {
  if (F.empty() || F.isDeclaration() || F.hasFnAttribute(Attribute::Naked))
    return false;
  // seed 初始化要插在入口终结指令前，musttail 尾块不允许插任何指令。
  if (hasMustTailReturn(F.getEntryBlock()))
    return false;

  // 获取程序所有单后继分支（无条件 br）。
  SmallVector<BranchInst *, 32> Branches;
  for (BasicBlock &BB : F)
    if (auto *Br = dyn_cast<BranchInst>(BB.getTerminator()))
      if (!Br->isConditional() && !isGeneratedBlock(BB))
        Branches.push_back(Br);
  if (Branches.empty())
    return false;

  // 按等级选择改写点（随机覆盖率），至少命中一个。
  unsigned Probability = Level >= 3 ? 100 : (Level == 2 ? Level2Probability
                                                        : Level1Probability);
  SmallVector<BranchInst *, 32> Sites;
  for (BranchInst *Br : Branches)
    if (Crypto.getRandom32() % 100 < Probability)
      Sites.push_back(Br);
  if (Sites.empty())
    Sites.push_back(Branches[Crypto.getRandom32() % Branches.size()]);

  SmallVector<Instruction *, 8> SeedChain;
  SmallVector<BasicBlock *, 8> Junks;
  auto Cleanup = [&]() {
    for (BasicBlock *BB : Junks)
      BB->eraseFromParent();
    eraseSeedChain(SeedChain);
  };

  AllocaInst *SeedSlot = createSeedSlot(F, SeedChain);

  // 生成随机指令的虚假块。
  unsigned JunkCount = 2 + Crypto.getRandom32() % 2 + (Level - 1);
  for (unsigned I = 0; I < JunkCount; ++I)
    Junks.push_back(createJunkBlockBody(F, SeedSlot, Crypto, I));

  // 获取程序所有块（真实块 + 虚假块）作为虚假后继候选池。
  SmallVector<BasicBlock *, 64> Pool;
  for (BasicBlock &BB : F)
    if (canBogusBranchTo(BB))
      Pool.push_back(&BB);
  if (Pool.empty()) {
    Cleanup();
    return false;
  }

  // 虚假块链接成链：链尾优先接回随机真实块（进入真实流的“活”路径），
  // 没有可用真实块时闭环回链头。
  BasicBlock *JunkHead = Junks[0];
  BasicBlock *TailTarget = JunkHead;
  for (BasicBlock *Candidate : Pool)
    if (!isGeneratedBlock(*Candidate)) {
      TailTarget = Candidate;
      break;
    }
  for (unsigned I = 0; I < Junks.size(); ++I) {
    BasicBlock *Next =
        I + 1 < Junks.size() ? Junks[I + 1] : TailTarget;
    IRBuilder<> IRB(Junks[I]);
    IRB.CreateBr(Next);
  }
  if (TailTarget != JunkHead)
    addEdgeFixPhi(Junks.back(), TailTarget);

  struct Rewrite {
    BranchInst *Br;
    BasicBlock *Target;
  };
  SmallVector<Rewrite, 32> Rewrites;
  for (BranchInst *Br : Sites) {
    BasicBlock *SourceBB = Br->getParent();
    BasicBlock *RealSucc = Br->getSuccessor(0);
    // 虚假后继从候选池随机选择（包括真实块与虚假块），跳过自身与
    // 真实后继（重复边不会增加混淆，还会制造重复 PHI incoming）。
    SmallVector<BasicBlock *, 64> Choices;
    for (BasicBlock *Candidate : Pool)
      if (Candidate != SourceBB && Candidate != RealSucc)
        Choices.push_back(Candidate);
    if (Choices.empty())
      continue;
    Rewrites.push_back({Br, Choices[Crypto.getRandom32() % Choices.size()]});
  }
  if (Rewrites.empty()) {
    Cleanup();
    return false;
  }

  // 锚定：随机选择可能一条虚假块边都抽不到，而未被引用的虚假块不会
  // 进入后端发射。强制把一条永假边的后继指到虚假块链头。
  if (none_of(Rewrites, [](const Rewrite &R) {
        return isGeneratedBlock(*R.Target);
      }))
    Rewrites[Crypto.getRandom32() % Rewrites.size()].Target = JunkHead;

  for (const Rewrite &R : Rewrites) {
    BasicBlock *SourceBB = R.Br->getParent();
    BasicBlock *RealSucc = R.Br->getSuccessor(0);

    // 单后继分支创建虚假分支：加密值链在分支前计算，比较值是
    // 不等于加密值的随机数，条件 icmp eq 永不为真——真边走真实
    // 后继，假边随机落进任意块。
    IRBuilder<> IRB(R.Br);
    Value *SeedValue = loadSeedVolatile(IRB, SeedSlot);
    uint32_t Encrypted;
    Value *EncryptedV = buildEncryptionChain(
        IRB, SeedValue, KnownSeedValue, Crypto, Encrypted);
    uint32_t Compare;
    do {
      Compare = Crypto.getRandom32() & 0xFFFF;
    } while (Compare == Encrypted);
    Value *Predicate = IRB.CreateICmpEQ(
        EncryptedV,
        ConstantInt::get(Type::getInt16Ty(F.getContext()), Compare),
        "vllvm.bcf.cmp");
    IRB.CreateCondBr(Predicate, R.Target, RealSucc);
    R.Br->eraseFromParent();

    addEdgeFixPhi(SourceBB, R.Target);
  }

  return true;
}

} // namespace

PreservedAnalyses BogusControlFlowPass::run(Function &F,
                                            FunctionAnalysisManager &FAM) {
  // bcf 混淆等级（Pass 自定语义）：1/2/3 对单后继分支的改写覆盖率
  // 递增（60%/85%/全量），虚假块数量随之增加；0=关闭。
  llvm::vllvm::VLLVMConfig &Config = llvm::vllvm::VLLVMConfig::get();
  Config.registerPassLevels("bcf", 1, 3);
  unsigned Level = Config.getLevel("bcf");
  if (Level == 0)
    return PreservedAnalyses::all();

  errs() << "[vllvm] BogusControlFlowPass:" << F.getName() << "\n";
  CryptoUtils Crypto(F.getParent());
  bool Changed = runBogusControlFlow(F, Level, Crypto);
  return Changed ? PreservedAnalyses::none() : PreservedAnalyses::all();
}
