#pragma once

#include "llvm/IR/Function.h"
#include "llvm/IR/PassManager.h"

using namespace llvm;

// 花指令 Pass：在标注函数内随机位置插入自包含花指令序列（模板库见
// c_func/flower/FlowerTemplates.h）。等级语义 level x = 每个标注函数
// 插入 x 条花指令（默认 1，0=关闭）。
class FlowerPass : public PassInfoMixin<FlowerPass> {
public:
  PreservedAnalyses run(Function &F, FunctionAnalysisManager &FAM);
  static bool isRequired() { return true; }
};
