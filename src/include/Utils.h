#ifndef __UTILS_OBF__
#define __UTILS_OBF__

#include "CryptoUtils.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/Transforms/Utils/Local.h" // For DemoteRegToStack and DemotePHIToStack

using namespace llvm;

class RandomizedIntegerCodec {
public:
  enum class Algorithm { Xor, Add, Sub };

  explicit RandomizedIntegerCodec(CryptoUtils &Crypto);

  Value *encode(IRBuilder<> &IRB, Value *Input, Value *Key,
                const Twine &Name = "") const;
  Value *decode(IRBuilder<> &IRB, Value *Input, Value *Key,
                const Twine &Name = "") const;
  Algorithm getAlgorithm() const { return SelectedAlgorithm; }

private:
  Algorithm SelectedAlgorithm;
};

bool valueEscapes(Instruction *Inst);
void fixStack(Function *f);
void fixStackForFlatten(Function *F);
CallBase *fixEH(CallBase *CB);
void insertFreeOnFunctionExits(Function &F, Value *Ptr);
void LowerConstantExpr(Function &F);
bool expandConstantExpr(Function &F);
#endif
