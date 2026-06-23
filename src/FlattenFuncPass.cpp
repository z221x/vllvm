#include "FlattenFuncPass.h"
#include "CryptoUtils.h"
#include "Utils.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Instructions.h"
#include "llvm/Transforms/Utils/LowerSwitch.h"

#include <algorithm>
#include <cstdint>
#include <random>
#include <vector>

namespace {
// 给 dispatch 树选择带随机扰动的切分点，避免每次都形成完全平衡的固定形态。
unsigned chooseDispatchSplit(unsigned Begin, unsigned End,
                             CryptoUtils &Crypto) {
  unsigned Count = End - Begin;
  if (Count <= 2)
    return Begin + 1;

  unsigned Base = Count / 2;
  unsigned Window = std::max(1U, Count / 4);
  unsigned Jitter = Crypto.getRandom32() % (Window * 2 + 1);
  int SignedSplit = static_cast<int>(Begin + Base) + static_cast<int>(Jitter) -
                    static_cast<int>(Window);
  return static_cast<unsigned>(std::clamp(
      SignedSplit, static_cast<int>(Begin + 1), static_cast<int>(End - 1)));
}

// 使用二叉路由树，替代旧的线性 caseDispatch 链。
BasicBlock *buildDispatchTree(Function &F, BasicBlock *InsertBefore,
                              Value *StateIndex, Value *CaseValue,
                              Type *Int32Ty, ArrayType *CaseTableTy,
                              GlobalVariable *CaseTable,
                              const std::vector<BasicBlock *> &FlattenBBs,
                              unsigned Begin, unsigned End,
                              BasicBlock *DefaultBB, CryptoUtils &Crypto) {
  LLVMContext &Ctx = F.getContext();
  BasicBlock *Node = BasicBlock::Create(
      Ctx, End - Begin == 1 ? "caseDispatch" : "caseRoute", &F, InsertBefore);
  IRBuilder<> IRB(Node);

  if (End - Begin == 1) {
    Value *CaseConstPtr = IRB.CreateInBoundsGEP(
        CaseTableTy, CaseTable,
        {ConstantInt::get(Int32Ty, 0), ConstantInt::get(Int32Ty, Begin)},
        "caseConstPtr");
    LoadInst *CaseConst =
        IRB.CreateLoad(Int32Ty, CaseConstPtr, "loadCaseConst");
    CaseConst->setVolatile(true);
    Value *CaseMatch = IRB.CreateICmpEQ(CaseValue, CaseConst, "caseConstMatch");
    IRB.CreateCondBr(CaseMatch, FlattenBBs[Begin], DefaultBB);
    return Node;
  }

  unsigned Split = chooseDispatchSplit(Begin, End, Crypto);
  BasicBlock *Left = buildDispatchTree(
      F, InsertBefore, StateIndex, CaseValue, Int32Ty, CaseTableTy, CaseTable,
      FlattenBBs, Begin, Split, DefaultBB, Crypto);
  BasicBlock *Right = buildDispatchTree(
      F, InsertBefore, StateIndex, CaseValue, Int32Ty, CaseTableTy, CaseTable,
      FlattenBBs, Split, End, DefaultBB, Crypto);

  Value *Threshold = ConstantInt::get(Int32Ty, Split);
  if (Crypto.getRandom32() & 1) {
    Value *GoLeft = IRB.CreateICmpULT(StateIndex, Threshold, "dispatch.left");
    IRB.CreateCondBr(GoLeft, Left, Right);
  } else {
    Value *GoRight = IRB.CreateICmpUGE(StateIndex, Threshold, "dispatch.right");
    IRB.CreateCondBr(GoRight, Right, Left);
  }
  return Node;
}

// 打乱基本块物理顺序，减少 CFG 视图按插入顺序排版时的规则感。
void shuffleFunctionBlocks(Function &F, CryptoUtils &Crypto) {
  if (F.size() <= 2)
    return;

  std::vector<BasicBlock *> Blocks;
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
} // namespace

PreservedAnalyses FlattenFuncPass::run(Function &F,
                                       FunctionAnalysisManager &FAM) {
  errs() << "[vllvm] FlattenFuncPass:" << F.getName() << "\n";
  bool isChanged = false;
  isChanged = doFlatten(F, FAM);
  return isChanged ? PreservedAnalyses::none() : PreservedAnalyses::all();
}
bool FlattenFuncPass::doFlatten(Function &F, FunctionAnalysisManager &FAM) {
  std::vector<BasicBlock *> flattenBBs;
  LowerSwitchPass lowerSwitchPass;
  lowerSwitchPass.run(F, FAM);
  LLVMContext &Ctx = F.getContext();
  Type *Int32Ty = Type::getInt32Ty(Ctx);
  BasicBlock *switchLoopEntry;
  BasicBlock *switchDispatchEntry;
  BasicBlock *switchLoopEnd;
  Module *M = F.getParent();
  // 初始化cryptoUtils
  CryptoUtils *cryptoUtils = new CryptoUtils(M);
  // 获取所有基本块
  for (Function::iterator bb = F.begin(); bb != F.end(); bb++) {

    flattenBBs.push_back(&(*bb));
  }
  if (flattenBBs.size() <= 1) {
    return false;
  }
  // 异常处理修正
  for (Function::iterator bb = F.begin(); bb != F.end(); bb++) {
    if (isa<InvokeInst>(bb->getTerminator())) {
      InvokeInst *inst = reinterpret_cast<InvokeInst *>(bb->getTerminator());
      auto removeBB = std::find(flattenBBs.begin(), flattenBBs.end(),
                                inst->getUnwindDest());
      if (removeBB != flattenBBs.end()) {
        flattenBBs.erase(removeBB);
      }
    }
  }
  flattenBBs.erase(flattenBBs.begin()); // 删除入口基本块
  if (flattenBBs.empty()) {
    return false;
  }
  // 入口基本块处理
  BasicBlock *entryBB = &*(F.begin());
  // 如果末尾指令是br
  BranchInst *br = NULL;
  if (isa<BranchInst>(entryBB->getTerminator())) {
    br = cast<BranchInst>(entryBB->getTerminator());
  }
  // 条件跳转与多分支跳转处理
  if (entryBB->getTerminator()->getNumSuccessors() > 1 &&
      (br != NULL && br->isConditional())) {
    BasicBlock::iterator i = entryBB->end();
    --i;

    if (entryBB->size() > 1) {
      --i;
    }

    // 将条件跳转的语句切割出来，成为一个新的基本块
    BasicBlock *tmpBB = entryBB->splitBasicBlock(i, "first");
    flattenBBs.insert(flattenBBs.begin(), tmpBB);
  }

  BasicBlock *initialStateBB = flattenBBs.front();
  std::default_random_engine shuffleEngine(cryptoUtils->getRandom32());
  std::shuffle(flattenBBs.begin(), flattenBBs.end(), shuffleEngine);

  auto getStateIndexForBB = [&](BasicBlock *targetBB) -> unsigned {
    for (unsigned i = 0; i < flattenBBs.size(); ++i) {
      if (flattenBBs[i] == targetBB) {
        return i;
      }
    }
    return flattenBBs.size() - 1;
  };

  IRBuilder<> IRB(entryBB);
  entryBB->getTerminator()->eraseFromParent(); // 删除原有的 terminator
  // 创建状态变量。状态值只保存常量表下标，真实比较常量放在表里。
  AllocaInst *switchVar = IRB.CreateAlloca(Int32Ty, 0, "switchVar");
  IRB.CreateStore(ConstantInt::get(Int32Ty, getStateIndexForBB(initialStateBB)),
                  switchVar);

  std::vector<uint32_t> caseValues;
  std::vector<Constant *> caseConstants;
  caseValues.reserve(flattenBBs.size());
  caseConstants.reserve(flattenBBs.size());
  for (unsigned i = 0; i < flattenBBs.size(); ++i) {
    uint32_t caseValue = cryptoUtils->getRandom32BaiscIndex(i);
    while (std::find(caseValues.begin(), caseValues.end(), caseValue) !=
           caseValues.end()) {
      caseValue = cryptoUtils->getRandom32();
    }
    caseValues.push_back(caseValue);
    caseConstants.push_back(ConstantInt::get(Int32Ty, caseValue));
  }

  ArrayType *caseTableTy = ArrayType::get(Int32Ty, caseConstants.size());
  GlobalVariable *caseTable =
      new GlobalVariable(*M, caseTableTy, true, GlobalValue::PrivateLinkage,
                         ConstantArray::get(caseTableTy, caseConstants),
                         (Twine("vllvm.fla.const.table.") + F.getName()).str());
  caseTable->setUnnamedAddr(GlobalValue::UnnamedAddr::Global);

  // switch框架
  switchLoopEntry = BasicBlock::Create(Ctx, "switchLoopEntry", &F, entryBB);
  switchDispatchEntry =
      BasicBlock::Create(Ctx, "switchDispatchEntry", &F, entryBB);
  switchLoopEnd = BasicBlock::Create(Ctx, "switchLoopEnd", &F, entryBB);
  // entryBB->switchLoopEntry
  entryBB->moveBefore(switchLoopEntry);
  BranchInst::Create(switchLoopEntry, entryBB);
  BranchInst::Create(switchLoopEntry, switchLoopEnd);
  BasicBlock *swDefault =
      BasicBlock::Create(Ctx, "switchDefault", &F, switchLoopEnd);
  BranchInst::Create(switchLoopEnd, swDefault);

  IRB.SetInsertPoint(switchLoopEntry);
  Value *stateIndex = IRB.CreateLoad(Int32Ty, switchVar, "loadSwitchIndex");
  Value *isValidIndex = IRB.CreateICmpULT(
      stateIndex, ConstantInt::get(Int32Ty, flattenBBs.size()),
      "switchIndexInRange");
  IRB.CreateCondBr(isValidIndex, switchDispatchEntry, swDefault);

  IRB.SetInsertPoint(switchDispatchEntry);
  Value *caseValuePtr = IRB.CreateInBoundsGEP(
      caseTableTy, caseTable, {ConstantInt::get(Int32Ty, 0), stateIndex},
      "caseValuePtr");
  LoadInst *caseValue = IRB.CreateLoad(Int32Ty, caseValuePtr, "loadCaseValue");
  caseValue->setVolatile(true);
  // 从 dispatcher 进入随机路由树，而不是顺序扫描每一个 case。
  BasicBlock *dispatchRoot = buildDispatchTree(
      F, swDefault, stateIndex, caseValue, Int32Ty, caseTableTy, caseTable,
      flattenBBs, 0, flattenBBs.size(), swDefault, *cryptoUtils);
  IRB.CreateBr(dispatchRoot);

  // 给每个块分配一个常量表下标作为状态值
  for (BasicBlock *bb : flattenBBs) {
    bb->moveBefore(switchLoopEnd);
  }
  // 修正每个原有基本块的后继
  for (BasicBlock *bb : flattenBBs) {
    // Ret块
    if (bb->getTerminator()->getNumSuccessors() == 0) {
      continue;
    }
    //
    if (isa<InvokeInst>(bb->getTerminator())) {
      continue;
    }
    if (bb->getTerminator()->getNumSuccessors() > 1 &&
        (isa<IndirectBrInst>(bb->getTerminator()))) {
      // errs() << "skip multi-branch flattening\n";
      continue;
    }
    IRBuilder<> IRB(bb);
    IRB.SetInsertPoint(bb->getTerminator());
    if (bb->getTerminator()->getNumSuccessors() == 1) {
      BasicBlock *succBB = bb->getTerminator()->getSuccessor(0);
      Value *oldStateIndex =
          IRB.CreateLoad(Int32Ty, switchVar, "oldStateIndex");
      Value *stateDelta =
          IRB.CreateSub(ConstantInt::get(Int32Ty, getStateIndexForBB(succBB)),
                        oldStateIndex, "stateDelta");
      Value *newStateIndex =
          IRB.CreateAdd(oldStateIndex, stateDelta, "newStateIndex");
      IRB.CreateStore(newStateIndex, switchVar);
      IRB.CreateBr(switchLoopEntry);
      bb->getTerminator()->eraseFromParent();
      continue;
    }
    if (bb->getTerminator()->getNumSuccessors() == 2) {
      unsigned trueIndex =
          getStateIndexForBB(bb->getTerminator()->getSuccessor(0));
      unsigned falseIndex =
          getStateIndexForBB(bb->getTerminator()->getSuccessor(1));
      // 创建分支
      BranchInst *br = cast<BranchInst>(bb->getTerminator());
      Value *targetStateIndex = IRB.CreateSelect(
          br->getCondition(), ConstantInt::get(Int32Ty, trueIndex),
          ConstantInt::get(Int32Ty, falseIndex), "targetStateIndex");
      Value *oldStateIndex =
          IRB.CreateLoad(Int32Ty, switchVar, "oldStateIndex");
      Value *stateDelta =
          IRB.CreateSub(targetStateIndex, oldStateIndex, "stateDelta");
      Value *newStateIndex =
          IRB.CreateAdd(oldStateIndex, stateDelta, "newStateIndex");
      IRB.CreateStore(newStateIndex, switchVar);
      IRB.CreateBr(switchLoopEntry);
      bb->getTerminator()->eraseFromParent();
      continue;
    }
  }
  //  收尾工作
  fixStack(&F);
  lowerSwitchPass.run(F, FAM);
  // 路由树和原始块都建好后再统一打乱布局顺序。
  shuffleFunctionBlocks(F, *cryptoUtils);
  return true;
}
