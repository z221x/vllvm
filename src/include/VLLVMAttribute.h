#pragma once

#include "VLLVM.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/Module.h"

namespace llvm::vllvm {

bool materializeAnnotationAttributes(Module &M);
bool hasVLLVMAttribute(Function &F, StringRef Kind);
VLLVMOptions getFunctionVLLVMOptions(Function &F);
bool moduleRequestsStringEncryption(Module &M);
bool moduleRequestsFunctionStringEncryption(Module &M);
bool hasVLLVMStringEncryptionAnnotation(GlobalVariable &GV);

} // namespace llvm::vllvm
