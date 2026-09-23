#include "IndirectCallPass.h"

#include "VLLVMAttribute.h"

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/Twine.h"
#include "llvm/IR/Attributes.h"
#include "llvm/IR/CallingConv.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/InlineAsm.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/RandomNumberGenerator.h"
#include "llvm/TargetParser/Triple.h"
#include "llvm/Transforms/Utils/ModuleUtils.h"

#include <algorithm>
#include <cstdint>
#include <random>
#include <string>
#include <utility>
#include <vector>

using namespace llvm;

namespace {
constexpr StringLiteral AppliedAttr = "vllvm.icall.applied";
constexpr StringLiteral RuntimeAttr = "vllvm.icall.runtime";
// 参数加密目标标记：入口带逆运算，调用点传密文。
constexpr StringLiteral CryptAttr = "vllvm.icall.crypt";
constexpr uint32_t MaxPoolDimension = 256;
constexpr uint32_t MaxPoolFunctions =
    MaxPoolDimension * MaxPoolDimension;

// 池内位置编码：group_id 占 16..23 位，index 占低 8 位，与 call_func
// 跳板里 ubfx w16, w19, #16, #8 / and w17, w19, #0xff 的取位一致。
struct PoolLocation {
  uint32_t Group = 0;
  uint32_t Index = 0;

  uint32_t pack() const { return (Group << 16) | Index; }
};

struct ICallRuntime {
  Function *CreatePool = nullptr;
  Function *RegisterFunc = nullptr;
  Function *CallFunc = nullptr;
};

// —— 参数加密：调用点按随机可逆运算链加密标量整数参数，经跳板传输的
// 是密文；icall 函数入口先做逆运算还原出明文参数。 ——

struct CryptOp {
  enum Kind : uint8_t { Add, Sub, Xor, MulOdd, Rol, Ror };
  Kind K;
  uint64_t C;
};

struct ArgCryptPlan {
  unsigned ArgPos = 0;
  SmallVector<CryptOp, 4> Ops;
};

// 奇数 C 模 2^W 的乘法逆元（牛顿迭代），是 MulOdd 的逆运算常量。
uint64_t mulInverseOdd(uint64_t C, unsigned W) {
  uint64_t Mask = W >= 64 ? ~0ULL : (1ULL << W) - 1;
  C &= Mask;
  uint64_t Inv = 1;
  for (unsigned I = 0; I < 6; ++I)
    Inv *= 2 - C * Inv;
  return Inv & Mask;
}

Value *emitFunnelShift(IRBuilder<> &B, Intrinsic::ID IID, Value *V,
                       uint64_t Shift, const Twine &Name) {
  return B.CreateIntrinsic(IID, {V->getType()},
                           {V, V, ConstantInt::get(V->getType(), Shift)},
                           nullptr, Name);
}

// 按生成顺序应用加密链。
Value *emitCryptForward(IRBuilder<> &B, Value *V, ArrayRef<CryptOp> Ops) {
  for (const CryptOp &Op : Ops) {
    Constant *Cst = ConstantInt::get(V->getType(), Op.C);
    switch (Op.K) {
    case CryptOp::Add:
      V = B.CreateAdd(V, Cst, "vllvm.icall.arg.crypt");
      break;
    case CryptOp::Sub:
      V = B.CreateSub(V, Cst, "vllvm.icall.arg.crypt");
      break;
    case CryptOp::Xor:
      V = B.CreateXor(V, Cst, "vllvm.icall.arg.crypt");
      break;
    case CryptOp::MulOdd:
      V = B.CreateMul(V, Cst, "vllvm.icall.arg.crypt");
      break;
    case CryptOp::Rol:
      V = emitFunnelShift(B, Intrinsic::fshl, V, Op.C, "vllvm.icall.arg.crypt");
      break;
    case CryptOp::Ror:
      V = emitFunnelShift(B, Intrinsic::fshr, V, Op.C, "vllvm.icall.arg.crypt");
      break;
    }
  }
  return V;
}

// 逆序应用各步逆运算；Head 返回链头（直接读取密文参数的指令），
// 替换参数使用时要把它排除在外。
Value *emitCryptInverse(IRBuilder<> &B, Value *V, ArrayRef<CryptOp> Ops,
                        Instruction **Head) {
  for (const CryptOp &Op : reverse(Ops)) {
    Type *Ty = V->getType();
    switch (Op.K) {
    case CryptOp::Add:
      V = B.CreateSub(V, ConstantInt::get(Ty, Op.C), "vllvm.icall.arg.plain");
      break;
    case CryptOp::Sub:
      V = B.CreateAdd(V, ConstantInt::get(Ty, Op.C), "vllvm.icall.arg.plain");
      break;
    case CryptOp::Xor:
      V = B.CreateXor(V, ConstantInt::get(Ty, Op.C), "vllvm.icall.arg.plain");
      break;
    case CryptOp::MulOdd: {
      Constant *Inv = ConstantInt::get(
          Ty, mulInverseOdd(Op.C, Ty->getIntegerBitWidth()));
      V = B.CreateMul(V, Inv, "vllvm.icall.arg.plain");
      break;
    }
    case CryptOp::Rol:
      V = emitFunnelShift(B, Intrinsic::fshr, V, Op.C, "vllvm.icall.arg.plain");
      break;
    case CryptOp::Ror:
      V = emitFunnelShift(B, Intrinsic::fshl, V, Op.C, "vllvm.icall.arg.plain");
      break;
    }
    if (*Head == nullptr)
      *Head = cast<Instruction>(V);
  }
  return V;
}

// 每个宽度 >= 8 的标量整数参数生成独立的随机运算链（长度 2..4，
// 全部由可逆操作组成）；指针/浮点/超窄整数保持明文。
std::vector<ArgCryptPlan>
buildArgCryptPlans(Function &F, std::default_random_engine &Engine) {
  std::uniform_int_distribution<unsigned> PickLen(2, 4);
  std::uniform_int_distribution<unsigned> PickKind(0, 5);
  std::uniform_int_distribution<uint64_t> PickConst(0, ~0ULL);

  std::vector<ArgCryptPlan> Plans;
  for (unsigned I = 0; I < F.arg_size(); ++I) {
    Type *Ty = F.getArg(I)->getType();
    if (!Ty->isIntegerTy() || Ty->getIntegerBitWidth() < 8)
      continue;
    unsigned W = Ty->getIntegerBitWidth();

    ArgCryptPlan Plan;
    Plan.ArgPos = I;
    for (unsigned K = 0, E = PickLen(Engine); K < E; ++K) {
      CryptOp Op;
      Op.K = static_cast<CryptOp::Kind>(PickKind(Engine));
      if (Op.K == CryptOp::MulOdd && W > 64)
        Op.K = CryptOp::Xor; // 超过 64 位没有 64 位乘法逆元可用
      if (Op.K == CryptOp::MulOdd)
        Op.C = PickConst(Engine) | 1ULL;
      else if (Op.K == CryptOp::Rol || Op.K == CryptOp::Ror)
        Op.C = PickConst(Engine) % W;
      else
        Op.C = PickConst(Engine);
      Plan.Ops.push_back(Op);
    }
    Plans.push_back(std::move(Plan));
  }
  return Plans;
}

// 判断对函数的一个常量引用是否只来自 llvm.global.annotations 标注表：
// annotate 标注机制自身的痕迹不算取地址。常量引用链向上走，碰到指令
// 或其他全局（如静态函数指针表）即视为真实取地址。
bool useIsAnnotationOnly(const Use &U, const GlobalVariable *Annotations) {
  SmallPtrSet<const User *, 8> Visited;
  SmallVector<const User *, 8> Stack;
  Stack.push_back(U.getUser());
  while (!Stack.empty()) {
    const User *Cur = Stack.pop_back_val();
    if (!Visited.insert(Cur).second)
      continue;
    if (isa<Instruction>(Cur))
      return false;
    if (const auto *GV = dyn_cast<GlobalVariable>(Cur))
      if (GV != Annotations)
        return false;
    for (const User *Parent : Cur->users())
      Stack.push_back(Parent);
  }
  return true;
}

// 地址被取用（存进函数指针表、作为回调传出去等）的目标存在无法改写
// 的间接调用，明文实参会直接命中解密入口，因此不能参与参数加密。
bool isFunctionAddressTaken(Function &F) {
  GlobalVariable *Annotations =
      F.getParent()->getGlobalVariable("llvm.global.annotations");
  for (Use &U : F.uses()) {
    User *Usr = U.getUser();
    auto *CB = dyn_cast<CallBase>(Usr);
    if (CB && CB->getCalledOperand()->stripPointerCasts() == &F)
      continue;
    if (isa<Constant>(Usr) && Annotations &&
        useIsAnnotationOnly(U, Annotations))
      continue;
    return true;
  }
  return false;
}

// 在函数入口插入逆运算链，并把函数体里对密文参数的使用全部换成明文。
void insertArgDecrypt(Function &F, ArrayRef<ArgCryptPlan> Plans) {
  IRBuilder<> B(&F.getEntryBlock().front());
  for (const ArgCryptPlan &Plan : Plans) {
    Argument *Arg = F.getArg(Plan.ArgPos);
    Instruction *Head = nullptr;
    Value *Plain = emitCryptInverse(B, Arg, Plan.Ops, &Head);
    // 链头读取的是密文参数本身，不能被替换；其余使用全部换成明文。
    Arg->replaceUsesWithIf(
        Plain, [&Head](Use &U) { return U.getUser() != Head; });
  }
}

void addRuntimeAttrs(Function &F) {
  F.addFnAttr(Attribute::NoInline);
  F.addFnAttr(Attribute::OptimizeNone);
  F.addFnAttr(Attribute::NoUnwind);
  F.addFnAttr(RuntimeAttr);
}

// 随机选 group_count，再推导 group_length；两者都不超过 8 位编码上限。
std::pair<uint32_t, uint32_t>
choosePoolShape(size_t FunctionCount, std::default_random_engine &Engine) {
  uint32_t MinGroups = static_cast<uint32_t>(
      (FunctionCount + MaxPoolDimension - 1) / MaxPoolDimension);
  uint32_t MaxGroups = static_cast<uint32_t>(
      std::min<size_t>(FunctionCount, MaxPoolDimension));
  std::uniform_int_distribution<uint32_t> PickGroupCount(MinGroups, MaxGroups);
  uint32_t GroupCount = PickGroupCount(Engine);
  uint32_t GroupLength = static_cast<uint32_t>(
      (FunctionCount + GroupCount - 1) / GroupCount);
  return {GroupCount, GroupLength};
}

Function *createCreatePoolFunction(Module &M, StringRef Name,
                                   GlobalVariable *GroupCountState,
                                   GlobalVariable *GroupLengthState) {
  LLVMContext &Ctx = M.getContext();
  Type *I32Ty = Type::getInt32Ty(Ctx);
  Function *F = Function::Create(
      FunctionType::get(Type::getVoidTy(Ctx), {I32Ty, I32Ty}, false),
      GlobalValue::InternalLinkage, Name, M);
  addRuntimeAttrs(*F);

  auto Arg = F->arg_begin();
  Value *GroupCount = &*Arg++;
  Value *GroupLength = &*Arg;
  IRBuilder<> IRB(BasicBlock::Create(Ctx, "entry", F));
  StoreInst *CountStore = IRB.CreateStore(GroupCount, GroupCountState);
  StoreInst *LengthStore = IRB.CreateStore(GroupLength, GroupLengthState);
  CountStore->setVolatile(true);
  LengthStore->setVolatile(true);
  IRB.CreateRetVoid();
  return F;
}

Function *createRegisterFunction(Module &M, StringRef Name,
                                 StructType *GroupTy, ArrayType *GroupsTy,
                                 GlobalVariable *Groups) {
  LLVMContext &Ctx = M.getContext();
  Type *I32Ty = Type::getInt32Ty(Ctx);
  Type *PtrTy = PointerType::getUnqual(Ctx);
  Function *F = Function::Create(
      FunctionType::get(Type::getVoidTy(Ctx), {I32Ty, I32Ty, PtrTy}, false),
      GlobalValue::InternalLinkage, Name, M);
  addRuntimeAttrs(*F);

  auto Arg = F->arg_begin();
  Value *GroupID = &*Arg++;
  Value *FuncIndex = &*Arg++;
  Value *FuncPtr = &*Arg;
  IRBuilder<> IRB(BasicBlock::Create(Ctx, "entry", F));
  Value *GroupPtr = IRB.CreateInBoundsGEP(
      GroupsTy, Groups, {IRB.getInt32(0), GroupID}, "vllvm.icall.group");
  Value *ArrayField = IRB.CreateStructGEP(
      GroupTy, GroupPtr, 1, "vllvm.icall.func_array.field");
  Value *FuncArray =
      IRB.CreateLoad(PtrTy, ArrayField, "vllvm.icall.func_array");
  Value *Slot = IRB.CreateInBoundsGEP(PtrTy, FuncArray, FuncIndex,
                                      "vllvm.icall.func_slot");
  StoreInst *Store = IRB.CreateStore(FuncPtr, Slot);
  Store->setVolatile(true);
  IRB.CreateRetVoid();
  return F;
}

// 生成共享跳板 call_func：index 由调用点经 icallcc 的 nest i32 送入 w19，
// 跳板按 (group_id, index) 查池并 ret 到真实目标，参数与返回值保持原 ABI。
Function *createCallFunc(Module &M, StringRef Name, GlobalVariable *PoolData) {
  LLVMContext &Ctx = M.getContext();
  Type *I32Ty = Type::getInt32Ty(Ctx);
  Function *F = Function::Create(
      FunctionType::get(Type::getVoidTy(Ctx), {I32Ty}, false),
      GlobalValue::InternalLinkage, Name, M);
  F->setCallingConv(CallingConv::ICall);
  F->addParamAttr(0, Attribute::Nest);
  addRuntimeAttrs(*F);
  F->addFnAttr(Attribute::Naked);

  std::string PoolSymbol = PoolData->getName().str();
  Triple TT(M.getTargetTriple());
  std::string Asm;
  if (TT.isOSBinFormatMachO()) {
    Asm = "ubfx w16, w19, #16, #8\n"
          "and w17, w19, #0xff\n"
          "adrp x9, _" +
          PoolSymbol +
          "@PAGE\n"
          "ldr x9, [x9, _" +
          PoolSymbol +
          "@PAGEOFF]\n"
          "add x9, x9, x16, lsl #4\n"
          "ldr x9, [x9, #8]\n"
          "ldr x19, [x9, x17, lsl #3]\n"
          "ret x19\n";
  } else {
    Asm = "ubfx w16, w19, #16, #8\n"
          "and w17, w19, #0xff\n"
          "adrp x9, " +
          PoolSymbol +
          "\n"
          "ldr x9, [x9, :lo12:" +
          PoolSymbol +
          "]\n"
          "add x9, x9, x16, lsl #4\n"
          "ldr x9, [x9, #8]\n"
          "ldr x19, [x9, x17, lsl #3]\n"
          "ret x19\n";
  }

  IRBuilder<> IRB(BasicBlock::Create(Ctx, "entry", F));
  FunctionType *AsmTy = FunctionType::get(Type::getVoidTy(Ctx), false);
  InlineAsm *Thunk = InlineAsm::get(AsmTy, Asm, "", true);
  IRB.CreateCall(AsmTy, Thunk);
  IRB.CreateUnreachable();
  return F;
}

ICallRuntime createRuntime(Module &M, uint32_t GroupCount,
                           uint32_t GroupLength, StringRef Suffix) {
  LLVMContext &Ctx = M.getContext();
  Type *I32Ty = Type::getInt32Ty(Ctx);
  Type *PtrTy = PointerType::getUnqual(Ctx);
  StructType *GroupTy = StructType::get(Ctx, {I32Ty, PtrTy});
  ArrayType *StorageTy =
      ArrayType::get(PtrTy, uint64_t(GroupCount) * GroupLength);
  ArrayType *GroupsTy = ArrayType::get(GroupTy, GroupCount);

  std::string Prefix = (Twine("__vllvm_icall.") + Suffix).str();
  auto *Storage = new GlobalVariable(
      M, StorageTy, false, GlobalValue::InternalLinkage,
      ConstantAggregateZero::get(StorageTy), Prefix + ".func_storage");

  SmallVector<Constant *, 16> GroupValues;
  Constant *Zero = ConstantInt::get(I32Ty, 0);
  for (uint32_t Group = 0; Group < GroupCount; ++Group) {
    Constant *Offset = ConstantInt::get(I32Ty, Group * GroupLength);
    Constant *StorageIndexes[] = {Zero, Offset};
    Constant *ArrayPtr = ConstantExpr::getInBoundsGetElementPtr(
        StorageTy, Storage, StorageIndexes);
    GroupValues.push_back(ConstantStruct::get(
        GroupTy, {ConstantInt::get(I32Ty, GroupLength), ArrayPtr}));
  }
  auto *Groups = new GlobalVariable(
      M, GroupsTy, true, GlobalValue::InternalLinkage,
      ConstantArray::get(GroupsTy, GroupValues), Prefix + ".groups");
  auto *PoolData = new GlobalVariable(
      M, PtrTy, true, GlobalValue::InternalLinkage, Groups,
      Prefix + ".func_pool_data");
  appendToCompilerUsed(M, {PoolData});

  auto *GroupCountState = new GlobalVariable(
      M, I32Ty, false, GlobalValue::InternalLinkage,
      ConstantInt::get(I32Ty, 0), Prefix + ".group_count");
  auto *GroupLengthState = new GlobalVariable(
      M, I32Ty, false, GlobalValue::InternalLinkage,
      ConstantInt::get(I32Ty, 0), Prefix + ".group_length");

  ICallRuntime Runtime;
  Runtime.CreatePool = createCreatePoolFunction(
      M, Prefix + ".create_func_pool", GroupCountState, GroupLengthState);
  Runtime.RegisterFunc = createRegisterFunction(
      M, Prefix + ".register_func", GroupTy, GroupsTy, Groups);
  Runtime.CallFunc =
      createCallFunc(M, Prefix + ".call_func", PoolData);
  return Runtime;
}

// init_array 注册：先建池再按乱序后的目标序列逐个登记函数地址。
// 注册必须早于用户构造函数，避免构造阶段首次调用时命中空槽。
void createRegistrationCtor(Module &M, const ICallRuntime &Runtime,
                            ArrayRef<Function *> Targets,
                            ArrayRef<PoolLocation> Locations,
                            uint32_t GroupCount, uint32_t GroupLength,
                            StringRef Suffix) {
  LLVMContext &Ctx = M.getContext();
  Function *Init = Function::Create(
      FunctionType::get(Type::getVoidTy(Ctx), false),
      GlobalValue::InternalLinkage,
      (Twine("__vllvm_icall.") + Suffix + ".register_funcs").str(), M);
  addRuntimeAttrs(*Init);

  IRBuilder<> IRB(BasicBlock::Create(Ctx, "entry", Init));
  IRB.CreateCall(Runtime.CreatePool,
                 {IRB.getInt32(GroupCount), IRB.getInt32(GroupLength)});
  for (size_t I = 0; I < Targets.size(); ++I)
    IRB.CreateCall(Runtime.RegisterFunc,
                   {IRB.getInt32(Locations[I].Group),
                    IRB.getInt32(Locations[I].Index), Targets[I]});
  IRB.CreateRetVoid();
  appendToGlobalCtors(M, Init, 0);
}

// icall 标签按被调方语义生效：带标签的函数进入注册池，模块内所有
// 调用点都要改走跳板。VMP/VMFlattenFunc 组合标记保持既有互斥，不进池。
bool shouldPoolFunction(Function &F) {
  if (F.empty() || F.isDeclaration() || F.isVarArg() ||
      F.getCallingConv() != CallingConv::C ||
      F.hasFnAttribute(AppliedAttr) || F.hasFnAttribute(Attribute::Naked))
    return false;

  llvm::vllvm::VLLVMOptions Options =
      llvm::vllvm::getFunctionVLLVMOptions(F);
  return Options.IndirectCall && !Options.Vmp && !Options.VMFlattenFunc;
}

// 只有普通 C 直调可以改写：musttail/operand bundle/可变参数调用与
// nest index 无法共存。命中池内目标时返回其 packed index。
const uint32_t *
getPooledIndex(CallInst &Call, const DenseMap<Function *, uint32_t> &Pool) {
  if (Call.isMustTailCall() || Call.hasOperandBundles() ||
      Call.getCallingConv() != CallingConv::C ||
      Call.getFunctionType()->isVarArg())
    return nullptr;

  Function *Callee =
      dyn_cast<Function>(Call.getCalledOperand()->stripPointerCasts());
  if (!Callee)
    return nullptr;
  for (unsigned I = 0; I < Call.arg_size(); ++I)
    if (Call.paramHasAttr(I, Attribute::Nest))
      return nullptr;

  auto It = Pool.find(Callee);
  return It == Pool.end() ? nullptr : &It->second;
}

AttributeList createICallAttrs(CallInst &OldCall) {
  LLVMContext &Ctx = OldCall.getContext();
  AttributeList OldAttrs = OldCall.getAttributes();
  SmallVector<AttributeSet, 8> ParamAttrs;
  for (unsigned I = 0; I < OldCall.arg_size(); ++I)
    ParamAttrs.push_back(OldAttrs.getParamAttrs(I));
  ParamAttrs.push_back(AttributeSet::get(
      Ctx, {Attribute::get(Ctx, Attribute::Nest)}));
  // 函数级 memory 等属性描述的是原目标，不适用于查表 trampoline。
  return AttributeList::get(Ctx, AttributeSet(), OldAttrs.getRetAttrs(),
                            ParamAttrs);
}

void rewriteCall(const ICallRuntime &Runtime, uint32_t PackedIndex,
                 CallInst *OldCall, ArrayRef<ArgCryptPlan> Crypt) {
  // 参与加密的参数先过正向运算链，再送进 icallcc 跳板。
  IRBuilder<> B(OldCall);
  SmallVector<Value *, 8> Args;
  for (unsigned I = 0; I < OldCall->arg_size(); ++I) {
    Value *Arg = OldCall->getArgOperand(I);
    for (const ArgCryptPlan &Plan : Crypt)
      if (Plan.ArgPos == I) {
        Arg = emitCryptForward(B, Arg, Plan.Ops);
        break;
      }
    Args.push_back(Arg);
  }
  Args.push_back(ConstantInt::get(Type::getInt32Ty(OldCall->getContext()),
                                  PackedIndex));
  SmallVector<Type *, 8> ParamTypes;
  for (Value *Arg : Args)
    ParamTypes.push_back(Arg->getType());
  FunctionType *ICallTy = FunctionType::get(
      OldCall->getType(), ParamTypes, false);
  CallInst *NewCall = CallInst::Create(
      ICallTy, Runtime.CallFunc, Args, OldCall->getName(),
      OldCall->getIterator());
  NewCall->setCallingConv(CallingConv::ICall);
  NewCall->setAttributes(createICallAttrs(*OldCall));
  NewCall->setDebugLoc(OldCall->getDebugLoc());
  NewCall->copyMetadata(*OldCall);

  if (!OldCall->getType()->isVoidTy())
    OldCall->replaceAllUsesWith(NewCall);
  OldCall->eraseFromParent();
}
} // namespace

PreservedAnalyses IndirectCallPass::run(Module &M,
                                        ModuleAnalysisManager &MAM) {
  (void)MAM;
  Triple TT(M.getTargetTriple());
  if (TT.getArch() != Triple::aarch64 && TT.getArch() != Triple::aarch64_be)
    return PreservedAnalyses::all();

  // 第一步：记录所有带 icall 标签的函数与总数。
  std::vector<Function *> Targets;
  for (Function &F : M)
    if (shouldPoolFunction(F))
      Targets.push_back(&F);
  if (Targets.empty())
    return PreservedAnalyses::all();
  if (Targets.size() > MaxPoolFunctions) {
    M.getContext().emitError(
        "vllvm icall supports at most 65536 pool functions per module");
    return PreservedAnalyses::all();
  }

  std::unique_ptr<RandomNumberGenerator> RNG = M.createRNG("vllvm.icall");
  std::default_random_engine Engine(static_cast<unsigned>((*RNG)()));
  auto [GroupCount, GroupLength] =
      choosePoolShape(Targets.size(), Engine);

  // 第二步：乱序后为每个函数分配 group_id 与 index，注册顺序即乱序序列。
  std::shuffle(Targets.begin(), Targets.end(), Engine);
  std::vector<PoolLocation> Locations(Targets.size());
  DenseMap<Function *, uint32_t> PackedIndexes;
  for (size_t I = 0; I < Targets.size(); ++I) {
    Locations[I] = {static_cast<uint32_t>(I / GroupLength),
                    static_cast<uint32_t>(I % GroupLength)};
    PackedIndexes.try_emplace(Targets[I], Locations[I].pack());
  }

  std::string Suffix = utohexstr((*RNG)());

  // 参数加密的前提是目标的所有调用点都会被改写：invoke、musttail、
  // operand bundle、可变参数、vmp 调用方保持明文直调，地址被取用的
  // 目标还存在间接调用，这些目标一律不加密。
  SmallPtrSet<Function *, 16> CryptBlocked;
  for (Function &F : M) {
    if (F.isDeclaration())
      continue;
    bool CallerIsVmp = llvm::vllvm::getFunctionVLLVMOptions(F).Vmp;
    for (Instruction &I : instructions(F)) {
      auto *CB = dyn_cast<CallBase>(&I);
      if (!CB)
        continue;
      Function *Callee =
          dyn_cast<Function>(CB->getCalledOperand()->stripPointerCasts());
      if (!Callee || !PackedIndexes.count(Callee))
        continue;
      auto *Call = dyn_cast<CallInst>(CB);
      if (Call && !CallerIsVmp && getPooledIndex(*Call, PackedIndexes))
        continue; // 该调用点会被改写并加密
      CryptBlocked.insert(Callee);
    }
  }
  DenseMap<Function *, std::vector<ArgCryptPlan>> CryptPlans;
  for (Function *Target : Targets) {
    if (CryptBlocked.count(Target) || isFunctionAddressTaken(*Target))
      continue;
    std::vector<ArgCryptPlan> Plans = buildArgCryptPlans(*Target, Engine);
    if (!Plans.empty()) {
      CryptPlans.try_emplace(Target, std::move(Plans));
      Target->addFnAttr(CryptAttr);
    }
  }

  ICallRuntime Runtime =
      createRuntime(M, GroupCount, GroupLength, Suffix);
  // 第三步：在 init_array 中注册所有函数。
  createRegistrationCtor(M, Runtime, Targets, Locations, GroupCount,
                         GroupLength, Suffix);

  // 第四步：改写模块内所有调用点。packed index 只在编译期存在，直接
  // 作为调用点常量经 icallcc 送入 w19；加密目标同时应用正向运算链。
  for (Function &F : M) {
    if (F.isDeclaration() || F.hasFnAttribute(RuntimeAttr))
      continue;
    // VMP 组合标记拥有优先级：虚拟化候选保持直接调用，否则 nest 参数会
    // 让 HOSTCALL 资格检查整体回退。
    if (llvm::vllvm::getFunctionVLLVMOptions(F).Vmp)
      continue;

    struct PlannedCall {
      CallInst *Call = nullptr;
      Function *Callee = nullptr;
      uint32_t Packed = 0;
    };
    SmallVector<PlannedCall, 16> Planned;
    for (Instruction &I : instructions(F))
      if (auto *Call = dyn_cast<CallInst>(&I))
        if (const uint32_t *Packed = getPooledIndex(*Call, PackedIndexes))
          Planned.push_back(
              {Call,
               cast<Function>(Call->getCalledOperand()->stripPointerCasts()),
               *Packed});
    if (Planned.empty())
      continue;
    errs() << "[vllvm] IndirectCallPass:" << F.getName() << "\n";
    for (const PlannedCall &P : Planned) {
      auto It = CryptPlans.find(P.Callee);
      rewriteCall(Runtime, P.Packed, P.Call,
                  It == CryptPlans.end()
                      ? ArrayRef<ArgCryptPlan>()
                      : ArrayRef(It->second));
    }
  }

  // 第五步：icall 函数入口插入逆运算，把参数还原成明文。
  for (auto &[Target, Plans] : CryptPlans)
    insertArgDecrypt(*Target, Plans);

  // 标记已入池，避免 pass 重复运行时二次建池。
  for (Function *Target : Targets)
    Target->addFnAttr(AppliedAttr);
  return PreservedAnalyses::none();
}
