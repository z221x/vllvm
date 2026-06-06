#include "PassInfo.h"
#include "VLLVM.h"

#include <utility>

using namespace llvm;

namespace llvm::vllvm {
void addVLLVMPasses(ModulePassManager &MPM, const VLLVMOptions &Options) {
  if (!Options.any())
    return;

  if (Options.EncryptoStr)
    MPM.addPass(EncryptoStrPass());

  FunctionPassManager FPM;
  bool HasFunctionPass = false;

  if (Options.FlattenFunc) {
    FPM.addPass(FlattenFuncPass());
    HasFunctionPass = true;
  }
  if (Options.IndirectCall) {
    FPM.addPass(IndirectCallPass());
    HasFunctionPass = true;
  }
  if (Options.IndirectBranch) {
    FPM.addPass(IndirectBranchPass());
    HasFunctionPass = true;
  }

  if (HasFunctionPass)
    MPM.addPass(createModuleToFunctionPassAdaptor(std::move(FPM)));
}
} // namespace llvm::vllvm
