#include "FlowerPass.h"

#include "CryptoUtils.h"
#include "c_func/flower/FlowerTemplates.h"
#include "config/VLLVMConfig.h"

#include "llvm/ADT/SmallVector.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/IRBuilder.h"

#include <algorithm>
#include <utility>

using namespace llvm;

namespace {

// musttail 尾块（call 紧邻 ret）不允许插入任何指令。
bool hasMustTailReturn(BasicBlock &BB) {
  auto *RI = dyn_cast<ReturnInst>(BB.getTerminator());
  if (!RI)
    return false;

  Instruction *Prev = RI->getPrevNonDebugInstruction();
  auto *CB = dyn_cast_or_null<CallBase>(Prev);
  return CB && CB->isMustTailCall();
}

// 收集函数内可作为花指令插入点的指令（锚点 = 插在该指令之前）：
// 跳过 EH pad 块（pad 指令必须位于块首）与 musttail 块、PHI（块首）
// 与调试/伪探针指令；入口块再跳过前导 alloca 段——静态 alloca 集中
// 保留在入口块头部，形态更接近真实编译产物。
SmallVector<Instruction *, 64> collectAnchors(Function &F) {
  SmallVector<Instruction *, 64> Anchors;
  for (BasicBlock &BB : F) {
    if (BB.isEHPad() || hasMustTailReturn(BB))
      continue;
    const bool IsEntry = BB.isEntryBlock();
    bool AfterPrologue = !IsEntry;
    for (Instruction &I : BB) {
      if (I.isDebugOrPseudoInst() || isa<PHINode>(I))
        continue;
      if (IsEntry && !AfterPrologue) {
        if (isa<AllocaInst>(I))
          continue;
        AfterPrologue = true;
      }
      Anchors.push_back(&I);
    }
  }
  return Anchors;
}

// Fisher-Yates 洗牌：随机化插入点顺序，前 level 个锚点即选中的位置。
void shuffleAnchors(SmallVectorImpl<Instruction *> &Anchors,
                    CryptoUtils &Crypto) {
  for (size_t I = Anchors.size(); I > 1; --I) {
    size_t J = Crypto.getRandom32() % I;
    std::swap(Anchors[I - 1], Anchors[J]);
  }
}

bool runFlower(Function &F, unsigned Level, CryptoUtils &Crypto) {
  if (F.empty() || F.isDeclaration() || F.hasFnAttribute(Attribute::Naked))
    return false;
  if (hasMustTailReturn(F.getEntryBlock()))
    return false;

  SmallVector<Instruction *, 64> Anchors = collectAnchors(F);
  if (Anchors.empty())
    return false;

  ArrayRef<vllvm::flower::FlowerTemplate> Templates =
      vllvm::flower::getFlowerTemplates();

  shuffleAnchors(Anchors, Crypto);
  unsigned Inserted = 0;
  for (unsigned I = 0; I < Level; ++I) {
    // 锚点数不足 level 时循环复用（同点可插多条）。
    Instruction *Anchor = Anchors[I % Anchors.size()];
    IRBuilder<> IRB(Anchor);
    // 随机起点轮询模板：位置前置条件不满足（如 stack 仅限入口块）时
    // 换下一个；模板大多无条件，整轮全不满足才会放弃该次插入。
    unsigned T = Crypto.getRandom32() % Templates.size();
    for (unsigned N = 0; N < Templates.size(); ++N) {
      if (Templates[(T + N) % Templates.size()].Emit(IRB, Crypto)) {
        ++Inserted;
        break;
      }
    }
  }
  return Inserted != 0;
}

} // namespace

PreservedAnalyses FlowerPass::run(Function &F, FunctionAnalysisManager &FAM) {
  // flower 混淆等级（Pass 自定语义）：level x = 每个标注函数插入
  // x 条花指令；0=关闭。函数过小（无可插锚点）时自然少于 x。
  vllvm::VLLVMConfig &Config = vllvm::VLLVMConfig::get();
  Config.registerPassLevels("flower", 1, 32);
  unsigned Level = Config.getLevel("flower");
  if (Level == 0)
    return PreservedAnalyses::all();

  errs() << "[vllvm] FlowerPass:" << F.getName() << "\n";
  CryptoUtils Crypto(F.getParent());
  bool Changed = runFlower(F, Level, Crypto);
  return Changed ? PreservedAnalyses::none() : PreservedAnalyses::all();
}
