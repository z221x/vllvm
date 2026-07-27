#include "llvm/Support/Compiler.h"

// VMP 直接写纯字节码流，不注册 MC asm backend、code emitter 或 object
// writer。该符号只用于 LLVMInitializeAllTargetMCs 的统一初始化表。
extern "C" LLVM_ABI LLVM_EXTERNAL_VISIBILITY void LLVMInitializeVMPTargetMC() {}
