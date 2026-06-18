#pragma once

#include "VLLVM.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Module.h"

namespace llvm::vllvm {

bool materializeAnnotationAttributes(Module &M);
bool hasVLLVMAttribute(Function &F, StringRef Kind);
VLLVMOptions getFunctionVLLVMOptions(Function &F,
                                     const VLLVMOptions &GlobalOptions);
bool moduleRequestsStringEncryption(Module &M,
                                    const VLLVMOptions &GlobalOptions);

} // namespace llvm::vllvm
