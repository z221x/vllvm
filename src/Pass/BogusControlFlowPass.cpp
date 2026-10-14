#include "BogusControlFlowPass.h"

#include "CryptoUtils.h"
#include "Utils.h"
#include "config/VLLVMConfig.h"

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Instructions.h"

#include <algorithm>
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

// seed 保存为每函数私有全局变量：值不能是明文常量，否则反编译器能顺
// 着变量把密文折叠掉、永假谓词直接消失。入口以全局自身地址（PIE 下含
// ASLR 基址，静态不可知）为不透明源，经恒等式 add(x, xor(x,-1)) 归一成
// 已知值 0xFFFF 后 volatile 写入：运行时恒为 KnownSeedValue，编译期与
// 反编译期均不可求解。初始化器为 0，不携带明文。
GlobalVariable *createSeedGlobal(Function &F,
                                 SmallVectorImpl<Instruction *> &Chain) {
  Module *M = F.getParent();
  Type *I16Ty = Type::getInt16Ty(F.getContext());
  auto *GV = new GlobalVariable(
      *M, I16Ty, /*isConstant=*/false, GlobalValue::PrivateLinkage,
      ConstantInt::get(I16Ty, 0),
      (Twine("vllvm.bcf.seed.") + F.getName()).str());
  GV->setAlignment(Align(2));

  BasicBlock *Entry = &F.getEntryBlock();
  IRBuilder<> IRB(Entry->getTerminator());
  // 显式创建 PtrToInt 指令：全局变量是 Constant，走 CreatePtrToInt 会
  // 被立即折叠成 ConstantExpr，推导不再是指令（且无法压栈跟踪回滚）。
  auto *Addr = new PtrToIntInst(GV, Type::getInt64Ty(F.getContext()));
  IRB.Insert(Addr);
  Addr->setName("vllvm.bcf.seed.addr");
  Value *X = IRB.CreateTrunc(Addr, I16Ty, "vllvm.bcf.seed.x");
  Value *NotX = IRB.CreateXor(X, ConstantInt::get(I16Ty, -1),
                              "vllvm.bcf.seed.not");
  Value *Norm = IRB.CreateAdd(X, NotX, "vllvm.bcf.seed.norm");
  StoreInst *Store = IRB.CreateStore(Norm, GV, "vllvm.bcf.seed.init");
  Store->setVolatile(true);

  for (Value *V : {cast<Value>(Addr), X, NotX, Norm, cast<Value>(Store)})
    Chain.push_back(cast<Instruction>(V));
  return GV;
}

LoadInst *loadSeedVolatile(IRBuilder<> &IRB, Value *SeedPtr) {
  LoadInst *Load = IRB.CreateLoad(Type::getInt16Ty(IRB.getContext()), SeedPtr,
                                  "vllvm.bcf.seed.load");
  Load->setVolatile(true);
  return Load;
}

// 随机算法运算加密：每个改写点对 seed 只做一次 add/sub/xor（无乘法、
// 不叠加多轮）。指令与镜像计算同步，加密后的值在编译期已知；全部
// 运算在 uint16（i16 回绕）上进行。
Value *applyEncryptionOp(IRBuilder<> &IRB, Value *Seed, uint32_t SeedValue,
                         CryptoUtils &Crypto, uint32_t &Encrypted) {
  Type *I16Ty = Type::getInt16Ty(IRB.getContext());
  uint32_t Expected = SeedValue & 0xFFFF;
  uint32_t K = Crypto.getRandom32() & 0xFFFF;
  Value *V = nullptr;

  switch (Crypto.getRandom32() % 3) {
  case 0:
    V = IRB.CreateAdd(Seed, ConstantInt::get(I16Ty, K), "vllvm.bcf.enc.add");
    Expected = (Expected + K) & 0xFFFF;
    break;
  case 1:
    V = IRB.CreateSub(Seed, ConstantInt::get(I16Ty, K), "vllvm.bcf.enc.sub");
    Expected = (Expected - K) & 0xFFFF;
    break;
  default:
    V = IRB.CreateXor(Seed, ConstantInt::get(I16Ty, K), "vllvm.bcf.enc.xor");
    Expected ^= K;
    break;
  }

  Encrypted = Expected;
  return V;
}

// 可克隆进虚假块的指令：排除终结指令/PHI（依附于前驱结构）、alloca
// （静态 alloca 只能留在入口块）、调用类（musttail/operand bundle/
// 副作用复杂）、EH pad、原子 RMW、调试与伪探针。
bool isCloneableForJunk(Instruction &I) {
  return !I.isTerminator() && !isa<PHINode>(I) && !isa<AllocaInst>(I) &&
         !isa<CallBase>(I) && !I.isEHPad() && !I.isDebugOrPseudoInst() &&
         !isa<AtomicCmpXchgInst>(I) && !isa<AtomicRMWInst>(I) &&
         !isa<FenceInst>(I);
}

// 跨块 SSA 操作数替换：虚假块不处于原支配链上，不能直接引用别处
// 定义的值。指针统一换成 seed 槽（opaque ptr 时代类型兼容），整数/
// 浮点换常量，其余换 undef；本块内已克隆的值经 Remap 复用。
Value *remapJunkOperand(Value *Op, DenseMap<Value *, Value *> &Remap,
                        Value *SeedPtr, CryptoUtils &Crypto) {
  auto It = Remap.find(Op);
  if (It != Remap.end())
    return It->second;
  if (!isa<Instruction>(Op))
    return Op; // 常量/实参/alloca/全局直接可用

  if (Op->getType()->isPointerTy())
    return SeedPtr;
  if (Op->getType()->isIntegerTy())
    return ConstantInt::get(Op->getType(), Crypto.getRandom32());
  if (Op->getType()->isFloatingPointTy())
    return ConstantFP::get(Op->getType(), 0.0);
  return UndefValue::get(Op->getType());
}

// 生成虚假块：volatile 读 seed 开头、结尾写回，中间以真实块的指令为
// 参考——从随机真实块克隆一段连续指令（保留原值名/调试位置/元数据，
// 反编译视图中与真实代码同形），跨块操作数按上规则替换。不设终结
// 指令，由调用方把虚假块链接成链并锚定到永假分支后继上，否则不可达
// 块不会进入后端发射，反编译时直接消失。
BasicBlock *createJunkBlockBody(
    Function &F, Value *SeedPtr, CryptoUtils &Crypto, unsigned Index,
    const SmallVectorImpl<SmallVector<Instruction *, 8>> &CloneSources) {
  BasicBlock *BB = BasicBlock::Create(
      F.getContext(), (BogusBlockPrefix + Twine(Index)).str(), &F);
  IRBuilder<> IRB(BB);

  Value *V = loadSeedVolatile(IRB, SeedPtr);
  if (CloneSources.empty()) {
    // 没有可克隆指令（极小函数）时退回单次随机运算。
    uint32_t Discarded;
    V = applyEncryptionOp(IRB, V, KnownSeedValue, Crypto, Discarded);
  } else {
    const SmallVectorImpl<Instruction *> &Src =
        CloneSources[Crypto.getRandom32() % CloneSources.size()];
    unsigned Start = Crypto.getRandom32() % Src.size();
    unsigned Count = std::min(2 + Crypto.getRandom32() % 4,
                              static_cast<unsigned>(Src.size() - Start));
    DenseMap<Value *, Value *> Remap;
    for (unsigned I = 0; I < Count; ++I) {
      Instruction *Cloned = Src[Start + I]->clone();
      IRB.Insert(Cloned);
      for (unsigned OpNo = 0; OpNo < Cloned->getNumOperands(); ++OpNo) {
        Value *Op = Cloned->getOperand(OpNo);
        if (isa<BasicBlock>(Op) || isa<MetadataAsValue>(Op))
          continue;
        Cloned->setOperand(OpNo, remapJunkOperand(Op, Remap, SeedPtr, Crypto));
      }
      Remap[Src[Start + I]] = Cloned;
    }
  }
  StoreInst *Store = IRB.CreateStore(V, SeedPtr, "vllvm.bcf.junk.store");
  Store->setVolatile(true);
  return BB;
}

void eraseSeedChain(MutableArrayRef<Instruction *> Chain) {
  for (Instruction *I : llvm::reverse(Chain))
    I->eraseFromParent();
}

// 前置处理 1：真实块大块切割。>= MinCutSize 条有效指令的块切成
// 2~4 条一段的碎片，碎片间以无条件 br 相连——既打散大块语义，切出的
// 无条件边本身又成为后续单后继分支伪造的改写点。切割点避开 PHIs（必须
// 留在块首）、入口块前导 alloca（静态 alloca 必须留在入口块）、EH pad
// 与 musttail 块。
bool cutLargeBlocks(Function &F, CryptoUtils &Crypto) {
  constexpr unsigned MinCutSize = 8;
  bool Split = false;

  SmallVector<BasicBlock *, 32> WorkList;
  for (BasicBlock &BB : F)
    if (!isGeneratedBlock(BB) && !BB.isEHPad() && !hasMustTailReturn(BB))
      WorkList.push_back(&BB);

  for (BasicBlock *BB : WorkList) {
    const bool IsEntry = BB->isEntryBlock();
    bool AfterPrologue = !IsEntry;

    SmallVector<Instruction *, 32> Anchors;
    unsigned Size = 0;
    for (Instruction &I : *BB) {
      if (I.isTerminator())
        break;
      if (I.isDebugOrPseudoInst())
        continue;
      ++Size;
      // 入口块的前导 alloca 段不可作为切割点。
      if (IsEntry && !AfterPrologue) {
        if (isa<AllocaInst>(I))
          continue;
        AfterPrologue = true;
      }
      if (isa<PHINode>(I))
        continue;
      Anchors.push_back(&I);
    }
    if (Size < MinCutSize || Anchors.size() < 2)
      continue;

    // 按随机段长 4~6 选取切割点（每段至少 4 条指令），尾部至少保留
    // 一条指令。
    SmallVector<Instruction *, 8> Cuts;
    unsigned Run = 0;
    unsigned Target = 4 + Crypto.getRandom32() % 3;
    for (unsigned Idx = 0; Idx < Anchors.size(); ++Idx) {
      ++Run;
      if (Run >= Target && Idx + 1 < Anchors.size()) {
        Cuts.push_back(Anchors[Idx]);
        Run = 0;
        Target = 4 + Crypto.getRandom32() % 3;
      }
    }
    if (Cuts.empty())
      continue;

    // splitBasicBlock 把切割点之后的指令移入新尾块；后续切割点位于
    // 逐次生成的尾块里，指令指针仍然有效。
    BasicBlock *Cur = BB;
    for (Instruction *Cut : Cuts) {
      Cur = Cur->splitBasicBlock(Cut->getIterator(), BB->getName());
    }
    Split = true;
  }
  return Split;
}

bool runBogusControlFlow(Function &F, unsigned Level, CryptoUtils &Crypto) {
  if (F.empty() || F.isDeclaration() || F.hasFnAttribute(Attribute::Naked))
    return false;
  // seed 初始化要插在入口终结指令前，musttail 尾块不允许插任何指令。
  if (hasMustTailReturn(F.getEntryBlock()))
    return false;

  // 前置处理 1：真实块大块切割，产生更多碎片与无条件边。
  bool Changed = cutLargeBlocks(F, Crypto);

  // 切割产生的碎片间值是跨块 SSA，而虚假边的随机后继会破坏原支配
  // 关系：所有跨块值（含 PHI）必须降为栈访问后才安全。不可降级的
  // 函数保持已切割形态回退，不再伪造虚假边。
  if (lowerPHINodes(F) == PHILoweringResult::Unsupported)
    return Changed;
  fixStackForFlatten(&F);

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
  GlobalVariable *SeedGV = nullptr;
  auto Cleanup = [&]() {
    for (BasicBlock *BB : Junks)
      BB->eraseFromParent();
    eraseSeedChain(SeedChain);
    if (SeedGV)
      SeedGV->eraseFromParent();
  };

  SeedGV = createSeedGlobal(F, SeedChain);

  // 前置处理 2：生成虚假块，指令以真实块为参考克隆。此刻真实块只含
  // 原程序指令与入口 seed 推导（谓词链在改写阶段才插入）。
  SmallVector<SmallVector<Instruction *, 8>, 32> CloneSources;
  for (BasicBlock &BB : F) {
    if (isGeneratedBlock(BB))
      continue;
    SmallVector<Instruction *, 8> Instrs;
    for (Instruction &I : BB)
      if (isCloneableForJunk(I))
        Instrs.push_back(&I);
    if (!Instrs.empty())
      CloneSources.push_back(std::move(Instrs));
  }

  unsigned JunkCount = 2 + Crypto.getRandom32() % 2 + (Level - 1);
  for (unsigned I = 0; I < JunkCount; ++I)
    Junks.push_back(createJunkBlockBody(F, SeedGV, Crypto, I, CloneSources));

  // 前置处理 3：所有块重排（入口固定首位），打散物理布局。
  shuffleFunctionBlocks(F, Crypto);

  // 获取程序所有块（真实块 + 虚假块）作为虚假后继候选池。
  SmallVector<BasicBlock *, 64> Pool;
  for (BasicBlock &BB : F)
    if (canBogusBranchTo(BB))
      Pool.push_back(&BB);
  if (Pool.empty()) {
    Cleanup();
    return Changed;
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
    return Changed;
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
    Value *SeedValue = loadSeedVolatile(IRB, SeedGV);
    uint32_t Encrypted;
    Value *EncryptedV =
        applyEncryptionOp(IRB, SeedValue, KnownSeedValue, Crypto, Encrypted);
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
