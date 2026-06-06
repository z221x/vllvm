#include "FlattenFuncPass.h"
#include "CryptoUtils.h"
#include "Utils.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Instructions.h"
#include "llvm/Transforms/Utils/LowerSwitch.h"
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
  PreservedAnalyses Changed = lowerSwitchPass.run(F, FAM);
  LLVMContext &Ctx = F.getContext();
  BasicBlock *switchLoopEntry;
  BasicBlock *switchLoopEnd;
  LoadInst *load;
  SwitchInst *switchI;
  Module *M = F.getParent();
  ConstantInt *Zero = ConstantInt::get(Type::getInt32Ty(Ctx), 0);
  GlobalVariable *globalSwitchVar =
      new GlobalVariable(*M,
                         Type::getInt32Ty(Ctx), // 类型
                         false,                 // isConstant
                         GlobalValue::ExternalLinkage,
                         Zero, // initializer -> 变为定义
                         "");
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
  IRBuilder<> IRB(entryBB);
  entryBB->getTerminator()->eraseFromParent(); // 删除原有的 terminator
  // 创建控制变量与初始化支配key
  IRB.CreateStore(ConstantInt::get(Type::getInt32Ty(Ctx),
                                   cryptoUtils->getRandom32BaiscIndex(0)),
                  globalSwitchVar);
  Constant *caseKeyValue = ConstantInt::get(Type::getInt32Ty(Ctx),
                                            cryptoUtils->getRandom32(), false);
  AllocaInst *caseKeyPtr =
      IRB.CreateAlloca(Type::getInt32Ty(Ctx), 0, "caseKeyPtr");
  IRB.CreateStore(caseKeyValue, caseKeyPtr);
  // switch框架
  switchLoopEntry = BasicBlock::Create(Ctx, "switchLoopEntry", &F, entryBB);
  switchLoopEnd = BasicBlock::Create(Ctx, "switchLoopEnd", &F, entryBB);
  load = new LoadInst(Type::getInt32Ty(Ctx), globalSwitchVar,
                      "loadGlobalSwitchVar", switchLoopEntry);
  // entryBB->switchLoopEntry
  entryBB->moveBefore(switchLoopEntry);
  BranchInst::Create(switchLoopEntry, entryBB);
  BranchInst::Create(switchLoopEntry, switchLoopEnd);
  BasicBlock *swDefault =
      BasicBlock::Create(Ctx, "switchDefault", &F, switchLoopEnd);
  BranchInst::Create(switchLoopEnd, swDefault);
  switchI = SwitchInst::Create(load, swDefault, 0, switchLoopEntry);
  // 给每个块分配case值
  for (BasicBlock *bb : flattenBBs) {
    ConstantInt *numCase = NULL;
    bb->moveBefore(switchLoopEnd);
    numCase = ConstantInt::get(Type::getInt32Ty(Ctx),
                               cryptoUtils->getRandom32BaiscIndex(
                                   switchI->getNumCases())); // 随机一个case值
    switchI->addCase(numCase, bb);
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
    ConstantInt *numCase = NULL;
    IRBuilder<> IRB(bb);
    IRB.SetInsertPoint(bb->getTerminator());
    if (bb->getTerminator()->getNumSuccessors() == 1) {
      BasicBlock *succBB = bb->getTerminator()->getSuccessor(0);
      numCase = switchI->findCaseDest(succBB);

      if (numCase == NULL) {
        numCase = ConstantInt::get(
            Type::getInt32Ty(Ctx),
            cryptoUtils->getRandom32BaiscIndex(switchI->getNumCases() - 1));
      }
      Constant *X = ConstantExpr::getSub(caseKeyValue, numCase);
      Value *caseKeyTmp =
          IRB.CreateLoad(Type::getInt32Ty(Ctx), caseKeyPtr, "caseKeyTmp");
      Value *newNumCase = IRB.CreateSub(caseKeyTmp, X, "");
      IRB.CreateStore(newNumCase, load->getPointerOperand());
      IRB.CreateBr(switchLoopEntry);
      bb->getTerminator()->eraseFromParent();
      continue;
    }
    if (bb->getTerminator()->getNumSuccessors() == 2) {
      ConstantInt *numCaseTrue =
          switchI->findCaseDest(bb->getTerminator()->getSuccessor(0));
      ConstantInt *numCaseFalse =
          switchI->findCaseDest(bb->getTerminator()->getSuccessor(1));

      if (numCaseTrue == NULL) {
        numCaseTrue = ConstantInt::get(
            Type::getInt32Ty(Ctx),
            cryptoUtils->getRandom32BaiscIndex(switchI->getNumCases() - 1));
      }
      if (numCaseFalse == NULL) {
        numCaseFalse = ConstantInt::get(
            Type::getInt32Ty(Ctx),
            cryptoUtils->getRandom32BaiscIndex(switchI->getNumCases() - 1));
      }
      Constant *X, *Y;
      X = ConstantExpr::getSub(caseKeyValue, numCaseTrue);
      Y = ConstantExpr::getSub(caseKeyValue, numCaseFalse);
      Value *caseKeyTmp =
          IRB.CreateLoad(Type::getInt32Ty(Ctx), caseKeyPtr, "caseKeyTmp");
      Value *newNumCaseTrue = IRB.CreateSub(caseKeyTmp, X, "newNumCaseTrue");
      Value *newNumCaseFalse = IRB.CreateSub(caseKeyTmp, Y, "newNumCaseFalse");
      // 创建分支
      BranchInst *br = cast<BranchInst>(bb->getTerminator());
      Value *sel = IRB.CreateSelect(br->getCondition(), newNumCaseTrue,
                                    newNumCaseFalse, "");

      IRB.CreateStore(sel, load->getPointerOperand());
      IRB.CreateBr(switchLoopEntry);
      bb->getTerminator()->eraseFromParent();
      continue;
    }
  }
  //  收尾工作
  fixStack(&F);
  lowerSwitchPass.run(F, FAM);
  return true;
}
