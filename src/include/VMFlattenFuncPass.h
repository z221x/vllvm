#pragma once
#include "llvm/IR/Function.h"
#include "llvm/IR/PassManager.h"

using namespace llvm;
class VMFlattenFuncPass : public PassInfoMixin<VMFlattenFuncPass> {
public:
  explicit VMFlattenFuncPass(bool RunBogusControlFlow = false)
      : RunBogusControlFlow(RunBogusControlFlow) {}

  PreservedAnalyses run(Function &F, FunctionAnalysisManager &FAM);
  static bool isRequired() { return true; }

private:
  bool runVMFlattenFunc(Function &F, FunctionAnalysisManager &FAM);
  bool RunBogusControlFlow = false;
};

// 把每函数常量表（fla 路由表等）改为参数传递：函数体搬到带表参数的
// 私有 vllvm.impl，原函数退化为传表包装器，并以 volatile 守卫伪调用点
// 阻断 -O2 过程间常量传播折叠回全局。indirectbr/musttail 场景整体回退。
// 由调度层在函数级混淆完成后调用；逻辑实现在 VMFlattenFuncPass.cpp。
bool moveTablesToImplParams(Function &F);
