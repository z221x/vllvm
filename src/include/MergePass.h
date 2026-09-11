#pragma once
#include "llvm/IR/Module.h"
#include "llvm/IR/PassManager.h"

using namespace llvm;
// Merge：模块级 pass。把带 vllvm.merge 标记的函数（bb2func 链路提取的
// helper 或手工标注的业务函数）按组融进 keyed dispatcher：keyed XOR
// selector 隐藏函数编号，成员体内联进 case 块后原函数退化为转发 wrapper
// 或被删除，从而抹掉 helper 身份与可读调用边界。
class MergePass : public PassInfoMixin<MergePass> {
public:
  PreservedAnalyses run(Module &M, ModuleAnalysisManager &MAM);
  static bool isRequired() { return true; }
};
