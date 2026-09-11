#ifndef __UTILS_OBF__
#define __UTILS_OBF__

#include "CryptoUtils.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/Transforms/Utils/Local.h" // For DemoteRegToStack and DemotePHIToStack

#include <cstdint>

using namespace llvm;

class RandomizedIntegerCodec {
public:
  enum class Mode { Mixed, Add, Xor, Sub, None };
  enum class Algorithm { Xor, Add, Sub };

  explicit RandomizedIntegerCodec(CryptoUtils &Crypto);

  Value *encode(IRBuilder<> &IRB, Value *Input, Value *Key,
                const Twine &Name = "") const;
  Value *decode(IRBuilder<> &IRB, Value *Input, Value *Key,
                const Twine &Name = "") const;
  Mode getMode() const { return SelectedMode; }
  Algorithm getAlgorithm() const { return SelectedAlgorithm; }

private:
  Value *mixKey(IRBuilder<> &IRB, Value *Key, const Twine &Name) const;

  Mode SelectedMode;
  Algorithm SelectedAlgorithm;
  uint64_t KeyMultiplier;
  uint64_t KeyXor;
};

bool valueEscapes(Instruction *Inst);
enum class PHILoweringResult { Unchanged, Lowered, Unsupported };
// 将 PHI 降为入口栈槽和前驱 store/汇合 load；不改写普通跨块 SSA 值。
// 不支持的函数不作任何修改，调用方必须回退，不能继续破坏其 CFG。
PHILoweringResult lowerPHINodes(Function &F);
void fixStack(Function *f);
void fixStackForFlatten(Function *F);
CallBase *fixEH(CallBase *CB);
void insertFreeOnFunctionExits(Function &F, Value *Ptr);
void LowerConstantExpr(Function &F);
bool expandConstantExpr(Function &F);
#endif
