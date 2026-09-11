#pragma once
#include "llvm/IR/Function.h"
#include "llvm/IR/PassManager.h"

using namespace llvm;
// BB2Func：把标记函数的 CFG 区域随机重组（先合并再切分）后用 CodeExtractor
// 提取成 noinline 内部 helper，打散源码级块边界；ChainToMerge 时给 helper
// 打上 vllvm.merge 标记，交给模块级 MergePass 做融合。
class BB2FuncPass : public PassInfoMixin<BB2FuncPass> {
public:
  explicit BB2FuncPass(bool ChainToMerge = false)
      : ChainToMerge(ChainToMerge) {}

  PreservedAnalyses run(Function &F, FunctionAnalysisManager &FAM);
  static bool isRequired() { return true; }

private:
  bool runBB2Func(Function &F);
  bool ChainToMerge = false;
};
