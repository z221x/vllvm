#include "PassInfo.h"
#include "BB2FuncPass.h"
#include "BogusControlFlowPass.h"
#include "MergePass.h"
#include "VLLVM.h"
#include "VLLVMAttribute.h"
#include "VmpPass.h"
#include "config/VLLVMConfig.h"

#include <utility>

using namespace llvm;

namespace llvm::vllvm {
namespace {
constexpr StringLiteral VmpInjectedNoInlineAttr = "vllvm.vmp.injected.noinline";
constexpr StringLiteral VmpHadAlwaysInlineAttr = "vllvm.vmp.had.alwaysinline";
// vmfla 链路标记：bb2func+merge 完成后，vmfla 在第二段函数级处理里执行。
constexpr StringLiteral VmFlattenPendingAttr = "vllvm.vmfla.pending";
// vmfla+ibr 同函数组合标记：ibr 必须在 vmfla 平坦化之后再执行，此处
// 记录延迟到晚期分发（缺陷 #3 的根因是反序执行）。
constexpr StringLiteral IbrLateAttr = "vllvm.ibr.late";

bool needsOptimizerProtection(const VLLVMOptions &Options) {
  return Options.VMFlattenFunc || Options.IndirectCall ||
         Options.IndirectBranch || Options.BogusControlFlow ||
         Options.BB2Func;
}

void protectFromLaterOptimization(Function &F) {
  // 控制流类混淆插在优化管线开头；如果不阻止后续 -O2，LLVM 可能把
  // wrapper/impl 内联、再把 CFG 和局部变量结构化访问重新化简掉。
  F.removeFnAttr(Attribute::AlwaysInline);
  F.addFnAttr(Attribute::NoInline);
  F.addFnAttr(Attribute::OptimizeNone);
}

class VLLVMAnnotationPass : public PassInfoMixin<VLLVMAnnotationPass> {
  bool OptEnabled;

public:
  VLLVMAnnotationPass(bool OptEnabled) : OptEnabled(OptEnabled) {}

  PreservedAnalyses run(Module &M, ModuleAnalysisManager &) {
    // 混淆不允许与编译优化同用：优化管线（IPSCCP/GlobalOpt 等）会把
    // 混淆形态改回可直接分析的形态（表参数折叠回全局、常量传播等），
    // 因此检测到 vllvm 标注且优化级别高于 -O0 时直接报错，拒绝产出
    // 形态失真的混淆二进制。无标注的普通编译不受影响。
    if (OptEnabled && moduleHasVLLVMAnnotations(M)) {
      M.getContext().emitError(
          "vllvm obfuscation cannot be combined with optimization: rebuild "
          "the annotated translation unit with -O0");
      return PreservedAnalyses::all();
    }
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

    // vmfla 等级为 0 时整条链路（bb2func+merge+vmfla）全局关闭，
    // 连第一段的 bb2func/merge 也不执行。
    VLLVMConfig::get().registerPassLevels("vmfla", 1, 2);
    if (FunctionOptions.VMFlattenFunc &&
        !VLLVMConfig::get().isEnabled("vmfla"))
      FunctionOptions.VMFlattenFunc = false;

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
      // vmfla 链路当前只保留平坦化本体：不再先切 bb2func helper + merge
      // 融合，直接挂 pending 延迟执行。bb2func/merge 作为保留方案仍可
      // 通过独立标注 vllvm:bb2func / vllvm:merge 使用；恢复链路时在此处
      // 加回 RunPass(BB2FuncPass(/*ChainToMerge=*/true))。
      F.addFnAttr(VmFlattenPendingAttr);
      FunctionOptions.VMFlattenFunc = false;
    }

    if (FunctionOptions.BB2Func) {
      RunPass(BB2FuncPass());
      FunctionOptions.BB2Func = false;
    }

    if (FunctionOptions.BogusControlFlow) {
      RunPass(BogusControlFlowPass());
      if (F.hasFnAttribute("vllvm.bcf")) {
        F.removeFnAttr("vllvm.bcf");
        PA.intersect(PreservedAnalyses::none());
      }
      FunctionOptions.BogusControlFlow = false;
    }

    // ibr 与 vmfla 组合时顺序必须是 vmfla 先平坦化、ibr 再对平坦化产物
    // 做地址表跳转：反序会让 vmfla 面对已被 ibr 改写成 indirectbr 的
    // CFG，地址表跳转网络被当孤岛排除在状态机外、入口边错接
    // dispatcher，函数体整体不执行（缺陷 #3）。同函数两者都标注时把
    // ibr 延迟到晚期分发紧随 vmfla 执行。
    if (FunctionOptions.IndirectBranch) {
      if (F.hasFnAttribute(VmFlattenPendingAttr))
        F.addFnAttr(IbrLateAttr);
      else
        RunPass(IndirectBranchPass());
    }

    // fla/lvars 的常量表改为参数传递：函数体搬到带表参数的私有 impl，
    // 原函数退化为传表包装器；没有表引用或不支持时保持原样。
    if (needsOptimizerProtection(FunctionOptions) &&
        moveTablesToImplParams(F))
      PA.intersect(PreservedAnalyses::none());

    return PA;
  }
};

// 第二段函数级处理：执行被链路延迟的 vmfla（bb2func + merge 之后）。
class VLLVMVMFlattenLateDispatchPass
    : public PassInfoMixin<VLLVMVMFlattenLateDispatchPass> {
public:
  static bool isRequired() { return true; }

  PreservedAnalyses run(Function &F, FunctionAnalysisManager &FAM) {
    if (!F.hasFnAttribute(VmFlattenPendingAttr))
      return PreservedAnalyses::all();
    F.removeFnAttr(VmFlattenPendingAttr);

    PreservedAnalyses PA = PreservedAnalyses::all();
    PA.intersect(VMFlattenFuncPass().run(F, FAM));

    // vmfla+ibr 组合：ibr 在平坦化之后执行。vmfla 把函数体搬进
    // <fn>.vllvm.impl（带表参数）并让原函数退化为传表包装器时，ibr
    // 作用于平坦化后的真实函数体；vmfla 整体回退（无 impl）时作用于
    // 原函数。
    if (F.hasFnAttribute(IbrLateAttr)) {
      F.removeFnAttr(IbrLateAttr);
      Function *Impl = F.getParent()->getFunction(
          (F.getName() + ".vllvm.impl").str());
      PA.intersect(
          IndirectBranchPass().run(Impl ? *Impl : F, FAM));
    }
    return PA;
  }
};
} // namespace

void addVLLVMPasses(ModulePassManager &MPM, OptimizationLevel Level) {
  MPM.addPass(VLLVMAnnotationPass(/*OptEnabled=*/Level.getSpeedupLevel() > 0));
  MPM.addPass(VLLVMEncryptoStrDispatchPass());
  // icall 需要先看完整 Module，统一去重、随机注册所有内部调用目标。
  MPM.addPass(IndirectCallPass());
  FunctionPassManager FPM;
  FPM.addPass(VLLVMFunctionDispatchPass());
  MPM.addPass(createModuleToFunctionPassAdaptor(std::move(FPM)));
  // merge 需要 Module 视角：把 vllvm.merge 标记函数（含 vmfla 链路里
  // bb2func 提取的 helper）按组融进 keyed dispatcher。
  MPM.addPass(MergePass());
  FunctionPassManager LateFPM;
  LateFPM.addPass(VLLVMVMFlattenLateDispatchPass());
  MPM.addPass(createModuleToFunctionPassAdaptor(std::move(LateFPM)));
}

void addVLLVMLatePasses(ModulePassManager &MPM) { MPM.addPass(VmpPass()); }
} // namespace llvm::vllvm
