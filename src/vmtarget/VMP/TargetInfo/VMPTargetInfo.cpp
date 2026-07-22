#include "TargetInfo/BPFTargetInfo.h"

#include "llvm/MC/TargetRegistry.h"
#include "llvm/Support/Compiler.h"

using namespace llvm;

Target &llvm::getTheBPFleTarget() {
  static Target TargetInstance;
  return TargetInstance;
}

Target &llvm::getTheBPFbeTarget() {
  static Target TargetInstance;
  return TargetInstance;
}

Target &llvm::getTheBPFTarget() {
  static Target TargetInstance;
  return TargetInstance;
}

extern "C" LLVM_ABI LLVM_EXTERNAL_VISIBILITY void
LLVMInitializeVMPTargetInfo() {
  // vmp 只通过 -march=vmp 显式选择，不冒充宿主 Triple 架构。
  TargetRegistry::RegisterTarget(getTheBPFTarget(), "vmp",
                                 "VLLVM virtual machine", "BPF",
                                 [](Triple::ArchType) { return false; });
}
