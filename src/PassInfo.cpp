#include "PassInfo.h"
#include "VLLVM.h"
#include "VLLVMAttribute.h"

#include <utility>

using namespace llvm;

namespace llvm::vllvm {
namespace {
bool needsOptimizerProtection(const VLLVMOptions &Options) {
  return Options.FunctionObfuscation || Options.FlattenFunc ||
         Options.IndirectCall || Options.IndirectBranch ||
         Options.LocalVarStruct || Options.BogusControlFlow;
}

void protectFromLaterOptimization(Function &F) {
  // 控制流类混淆插在优化管线开头；如果不阻止后续 -O2，LLVM 可能把
  // wrapper/impl 内联、再把 CFG 和局部变量结构化访问重新化简掉。
  F.removeFnAttr(Attribute::AlwaysInline);
  F.addFnAttr(Attribute::NoInline);
  F.addFnAttr(Attribute::OptimizeNone);
}

class VLLVMAnnotationPass : public PassInfoMixin<VLLVMAnnotationPass> {
public:
  PreservedAnalyses run(Module &M, ModuleAnalysisManager &) {
    return materializeAnnotationAttributes(M) ? PreservedAnalyses::none()
                                              : PreservedAnalyses::all();
  }
};

class VLLVMEncryptoStrDispatchPass
    : public PassInfoMixin<VLLVMEncryptoStrDispatchPass> {
public:
  PreservedAnalyses run(Module &M, ModuleAnalysisManager &MAM) {
    if (!moduleRequestsStringEncryption(M))
      return PreservedAnalyses::all();
    return EncryptoStrPass().run(M, MAM);
  }
};

class VLLVMFunctionDispatchPass
    : public PassInfoMixin<VLLVMFunctionDispatchPass> {
public:
  static bool isRequired() { return true; }

  PreservedAnalyses run(Function &F, FunctionAnalysisManager &FAM) {
    VLLVMOptions FunctionOptions = getFunctionVLLVMOptions(F);
    FunctionOptions.EncryptoStr = false;
    if (!FunctionOptions.any())
      return PreservedAnalyses::all();

    if (needsOptimizerProtection(FunctionOptions))
      protectFromLaterOptimization(F);

    PreservedAnalyses PA = PreservedAnalyses::all();
    auto RunPass = [&](auto Pass) { PA.intersect(Pass.run(F, FAM)); };

    if (FunctionOptions.FunctionObfuscation) {
      RunPass(FunctionObfuscationPass(FunctionOptions.BogusControlFlow));
      FunctionOptions.BogusControlFlow = false;
    }

    if (FunctionOptions.BogusControlFlow) {
      RunPass(BogusControlFlowPass());
      if (F.hasFnAttribute("vllvm.bcf")) {
        F.removeFnAttr("vllvm.bcf");
        PA.intersect(PreservedAnalyses::none());
      }
      FunctionOptions.BogusControlFlow = false;
    }

    if (FunctionOptions.IndirectCall)
      RunPass(IndirectCallPass());
    if (FunctionOptions.LocalVarStruct)
      RunPass(LocalVarStructPass());
    if (FunctionOptions.FlattenFunc)
      RunPass(FlattenFuncPass());

    if (FunctionOptions.IndirectBranch)
      RunPass(IndirectBranchPass());

    return PA;
  }
};
} // namespace

void addVLLVMPasses(ModulePassManager &MPM) {
  MPM.addPass(VLLVMAnnotationPass());
  MPM.addPass(VLLVMEncryptoStrDispatchPass());
  FunctionPassManager FPM;
  FPM.addPass(VLLVMFunctionDispatchPass());
  MPM.addPass(createModuleToFunctionPassAdaptor(std::move(FPM)));
}
} // namespace llvm::vllvm
