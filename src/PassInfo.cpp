#include "PassInfo.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Passes/PassPlugin.h"
using namespace llvm;
// 定义插件的入口函数，注册 Pass 到 Pass 管理器
llvm::PassPluginLibraryInfo getPassPluginInfo() {
  // 返回插件的元信息
  return {LLVM_PLUGIN_API_VERSION, // LLVM 插件的 API 版本
          "vllvm",                 // 插件名称
          LLVM_VERSION_STRING,     // 当前 LLVM 的版本号
          [](PassBuilder &PB) { // 一个回调，用于将 Pass 注册到 PassBuilder 中
            // 注册管道解析回调函数，用于支持命令行参数调用
            PB.registerPipelineParsingCallback(
                [](StringRef Name, ModulePassManager &MPM,
                   ArrayRef<PassBuilder::PipelineElement>) {
                  bool isPass = false;
                  // 根据空格分割字符串
                  while (!Name.empty()) {
                    StringRef passName = Name.split(' ').first;
                    Name = Name.split(' ').second;
                    if (passName == "en-str") {
                      MPM.addPass(EncryptoStrPass());
                      isPass = true;
                    }
                  }

                  return isPass; // 未匹配，跳过此 Pass
                });
            PB.registerPipelineParsingCallback(
                [](StringRef Name, FunctionPassManager &FPM,
                   ArrayRef<PassBuilder::PipelineElement>) {
                  bool isPass = false;
                  while (!Name.empty()) {
                    StringRef passName = Name.split(' ').first;
                    Name = Name.split(' ').second;
                    if (passName == "flatten-func") {
                      FPM.addPass(FlattenFuncPass());
                      isPass = true;
                    }
                    if (passName == "indirect-call") {
                      FPM.addPass(IndirectCallPass());
                      isPass = true;
                    }
                    if (passName == "indirect-br") {
                      FPM.addPass(IndirectBranchPass());
                      isPass = true;
                    }
                  }
                  return isPass;
                });
          }};
}
// 必须导出符号 `llvmGetPassPluginInfo`，这是 LLVM 插件的入口点
extern "C" LLVM_ATTRIBUTE_WEAK ::llvm::PassPluginLibraryInfo
llvmGetPassPluginInfo() {
  return getPassPluginInfo(); // 调用自定义的插件入口函数
}
