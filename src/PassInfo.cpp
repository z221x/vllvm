#include "PassInfo.h"
#include "VLLVM.h"
#include "VLLVMAttribute.h"

#include <utility>

using namespace llvm;

namespace llvm::vllvm {
namespace {
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

    PreservedAnalyses PA = PreservedAnalyses::all();
    auto RunPass = [&](auto Pass) { PA.intersect(Pass.run(F, FAM)); };

    bool UseCombinedConstTable = FunctionOptions.FlattenFunc &&
                                 FunctionOptions.IndirectCall &&
                                 FunctionOptions.LocalVarStruct;
    if (UseCombinedConstTable) {
      RunPass(FunctionObfuscationPass());
    } else {
      if (FunctionOptions.FlattenFunc)
        RunPass(FlattenFuncPass());
      if (FunctionOptions.IndirectCall)
        RunPass(IndirectCallPass());
      if (FunctionOptions.LocalVarStruct)
        RunPass(LocalVarStructPass());
    }

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
