#include "PassInfo.h"
#include "VLLVM.h"
#include "VLLVMAttribute.h"
#include "VmpPass.h"

#include <utility>

using namespace llvm;

namespace llvm::vllvm {
namespace {
constexpr StringLiteral VmpInjectedNoInlineAttr = "vllvm.vmp.injected.noinline";
constexpr StringLiteral VmpHadAlwaysInlineAttr = "vllvm.vmp.had.alwaysinline";

bool needsOptimizerProtection(const VLLVMOptions &Options) {
  return Options.VMFlattenFunc || Options.FlattenFunc ||
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

    // VMP 在 OptimizerLast 独立处理。它与函数级混淆组合时拥有优先级，避免
    // 先改写 CFG 后使虚拟化资格和语义不可预测。
    if (FunctionOptions.Vmp) {
      const bool HadAlwaysInline = F.hasFnAttribute(Attribute::AlwaysInline);
      const bool HadNoInline = F.hasFnAttribute(Attribute::NoInline);
      if (HadAlwaysInline) {
        F.removeFnAttr(Attribute::AlwaysInline);
        F.addFnAttr(VmpHadAlwaysInlineAttr);
      }
      if (!HadNoInline) {
        F.addFnAttr(Attribute::NoInline);
        F.addFnAttr(VmpInjectedNoInlineAttr);
      }
      return HadAlwaysInline || !HadNoInline ? PreservedAnalyses::none()
                                             : PreservedAnalyses::all();
    }

    if (needsOptimizerProtection(FunctionOptions))
      protectFromLaterOptimization(F);

    PreservedAnalyses PA = PreservedAnalyses::all();
    auto RunPass = [&](auto Pass) { PA.intersect(Pass.run(F, FAM)); };

    if (FunctionOptions.VMFlattenFunc) {
      RunPass(VMFlattenFuncPass(FunctionOptions.BogusControlFlow));
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
  // icall 需要先看完整 Module，统一去重、随机注册所有内部调用目标。
  MPM.addPass(IndirectCallPass());
  FunctionPassManager FPM;
  FPM.addPass(VLLVMFunctionDispatchPass());
  MPM.addPass(createModuleToFunctionPassAdaptor(std::move(FPM)));
}

void addVLLVMLatePasses(ModulePassManager &MPM) { MPM.addPass(VmpPass()); }
} // namespace llvm::vllvm
