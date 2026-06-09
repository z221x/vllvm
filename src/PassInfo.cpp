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
  bool UseCombinedConstTable =
      Options.FlattenFunc && Options.IndirectCall && Options.LocalVarStruct;

  if (UseCombinedConstTable) {
    FPM.addPass(CombinedObfuscationPass());
    HasFunctionPass = true;
  } else if (Options.FlattenFunc) {
    FPM.addPass(FlattenFuncPass());
    HasFunctionPass = true;
  }
  if (!UseCombinedConstTable && Options.IndirectCall) {
    FPM.addPass(IndirectCallPass());
    HasFunctionPass = true;
  }
  if (Options.IndirectBranch) {
    FPM.addPass(IndirectBranchPass());
    HasFunctionPass = true;
  }
  if (!UseCombinedConstTable && Options.LocalVarStruct) {
    FPM.addPass(LocalVarStructPass());
    HasFunctionPass = true;
  }

  if (HasFunctionPass)
    MPM.addPass(createModuleToFunctionPassAdaptor(std::move(FPM)));
}
} // namespace llvm::vllvm
