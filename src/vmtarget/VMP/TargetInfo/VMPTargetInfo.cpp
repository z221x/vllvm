#include "VMPTargetInfo.h"

#include "llvm/MC/TargetRegistry.h"
#include "llvm/Support/Compiler.h"

using namespace llvm;

Target &llvm::getTheVMPTarget() {
  static Target Instance;
  return Instance;
}

extern "C" LLVM_ABI LLVM_EXTERNAL_VISIBILITY void
LLVMInitializeVMPTargetInfo() {
  // 不增加公开 Triple 枚举；必须通过 -march=vmp 显式选择。
  TargetRegistry::RegisterTarget(getTheVMPTarget(), "vmp",
                                 "VLLVM virtual machine", "VMP",
                                 [](Triple::ArchType) { return false; });
}
