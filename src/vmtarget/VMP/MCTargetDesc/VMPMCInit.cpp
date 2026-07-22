#include "llvm/Support/Compiler.h"

extern "C" LLVM_ABI void LLVMInitializeBPFTargetMC();

extern "C" LLVM_ABI LLVM_EXTERNAL_VISIBILITY void LLVMInitializeVMPTargetMC() {
  LLVMInitializeBPFTargetMC();
}
