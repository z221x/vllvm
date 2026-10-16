#include "IndirectCallPass.h"

#include "VLLVMAttribute.h"
#include "config/VLLVMConfig.h"

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
  // 跳板 naked 汇编查表的池数据全局（ptr -> groups）。
  GlobalVariable *PoolData = nullptr;
};

// —— 参数加密：每个改写调用点有专属跳板函数（签名与被调函数一致），
// 调用者明文直调跳板；跳板内对参数应用随机可逆运算链（仅 add/sub/xor）
// 并查池间接跳转到目标；目标入口把参数解密一次写回参数槽位，之后按
// 左值（volatile 槽位读取）使用。加密在跳板、解密在被调函数，调用者不参与。 ——

struct CryptOp {
  enum Kind : uint8_t { Add, Sub, Xor };
  Kind K;
  uint64_t C;
};

struct ArgCryptPlan {
  unsigned ArgPos = 0;
  // 参数所在的 x 寄存器序号（0..7）：加密链在跳板的 naked 汇编里就地
  // 改写该寄存器；栈传参数不参与加密。
  unsigned Reg = 0;
  // 运算链位宽：整数按自身位宽（32/64），指针按指针位宽（64），入口
  // 解密经 ptrtoint/inttoptr 往返。
  unsigned Width = 64;
  bool IsPointer = false;
  SmallVector<CryptOp, 4> Ops;
};

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
    }
    if (*Head == nullptr)
      *Head = cast<Instruction>(V);
  }
  return V;
}

// 每个经 x0~x7 寄存器传入的标量整数参数（32/64 位）与普通地址空间指针
// 参数生成独立的随机运算链（长度 2..4，全部由可逆操作组成），x0~x7 上
// 的整数类寄存器都可能被加密；浮点（v 寄存器）、栈传参数、8/16/128 位
// 整数、arm64e/PAC 目标的指针保持明文。
std::vector<ArgCryptPlan>
buildArgCryptPlans(Function &F, std::default_random_engine &Engine) {
  std::uniform_int_distribution<unsigned> PickLen(2, 4);
  std::uniform_int_distribution<unsigned> PickKind(0, 2);
  std::uniform_int_distribution<uint64_t> PickConst(0, ~0ULL);

  bool IsArm64e = Triple(F.getParent()->getTargetTriple()).isArm64e();
  std::vector<ArgCryptPlan> Plans;
  unsigned XReg = 0;
  // x 寄存器序号只在参数形态简单时可静态推算：sret 走 x8 不占 x0~x7；
  // 聚合类型（struct/array/vector）与 i128 占用的寄存器数依赖完整
  // AAPCS 分类，遇到之后序号不再可靠，后续参数一律不加密。
  bool Tracking = true;
  for (unsigned I = 0; I < F.arg_size(); ++I) {
    Argument *Arg = F.getArg(I);
    Type *Ty = Arg->getType();
    if (Arg->hasAttribute(Attribute::StructRet))
      continue;
    bool Complex = Ty->isStructTy() || Ty->isArrayTy() || Ty->isVectorTy() ||
                   (Ty->isIntegerTy() && Ty->getIntegerBitWidth() > 64);
    if (Complex) {
      Tracking = false;
      continue;
    }
    bool IntClass = Ty->isIntegerTy() || Ty->isPointerTy();
    unsigned Reg = XReg;
    if (IntClass)
      ++XReg;
    if (!Tracking || !IntClass || Reg >= 8)
      continue; // 浮点在 v 寄存器；栈传参数不在 x0~x7 上

    bool IsPointer = Ty->isPointerTy();
    unsigned W;
    if (IsPointer) {
      // 改写带 PAC 签名/特殊地址空间的指针会破坏签名或被运行时拦截。
      if (IsArm64e || Ty->getPointerAddressSpace() != 0)
        continue;
      W = F.getDataLayout().getPointerSizeInBits(0);
    } else {
      W = Ty->getIntegerBitWidth();
      if (W != 32 && W != 64)
        continue;
    }

    ArgCryptPlan Plan;
    Plan.ArgPos = I;
    Plan.Reg = Reg;
    Plan.Width = W;
    Plan.IsPointer = IsPointer;
    for (unsigned K = 0, E = PickLen(Engine); K < E; ++K) {
      CryptOp Op;
      Op.K = static_cast<CryptOp::Kind>(PickKind(Engine));
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

// 在函数入口把参数解密一次，写回 IRGen 的参数 alloca 槽位；之后对该
// 槽位的读取全部改为 volatile。这样「参数 = 解密的参数」之后的使用都
// 是左值（槽位变量）读取：mem2reg 不能提升 volatile 槽位，后端也无法
// 把解密算术重物化回各使用点的密文表达式。
void insertArgDecrypt(Function &F, ArrayRef<ArgCryptPlan> Plans) {
  for (const ArgCryptPlan &Plan : Plans) {
    Argument *Arg = F.getArg(Plan.ArgPos);
    // IRGen 的参数初始化 store：把写入槽位的密文换成明文。
    StoreInst *Init = nullptr;
    for (User *U : Arg->users())
      if (auto *SI = dyn_cast<StoreInst>(U); SI && SI->getValueOperand() == Arg) {
        Init = SI;
        break;
      }
    IRBuilder<> B(Init ? static_cast<Instruction *>(Init)
                       : &F.getEntryBlock().front());
    // 指针参数经 ptrtoint 参与整数逆链，还原后 inttoptr 回指针类型。
    // Exclude 是直接读取密文参数的链头（指针为 ptrtoint，整数为首个
    // 逆运算指令），替换使用时要排除，否则自引用成环。
    Instruction *Exclude = nullptr;
    Value *Base = Arg;
    if (Plan.IsPointer) {
      auto *PI = cast<Instruction>(B.CreatePtrToInt(
          Arg, Type::getIntNTy(F.getContext(), Plan.Width)));
      Base = PI;
      Exclude = PI;
    }
    Value *Plain = emitCryptInverse(B, Base, Plan.Ops, &Exclude);
    if (Plan.IsPointer)
      Plain = B.CreateIntToPtr(Plain, Arg->getType());
    // 密文参数的所有使用都换成明文：包括初始化 store 的值操作数，以及
    // byval 等不经槽位、直接使用参数值的形态。
    Arg->replaceUsesWithIf(
        Plain, [&Exclude](Use &U) { return U.getUser() != Exclude; });
    if (!Init)
      continue;
    Value *Slot = Init->getPointerOperand();
    for (User *U : Slot->users())
      if (auto *LI = dyn_cast<LoadInst>(U))
        LI->setVolatile(true);
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
  Runtime.PoolData = PoolData;
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

void rewriteCall(Function &Trampoline, uint32_t PackedIndex, CallInst *OldCall) {
  // 调用点退化为对专属跳板的直调：明文实参、无加密运算，调用者函数体
  // 保持干净。调用号（group_id<<16|index）作为调用点常量经尾部 i32 nest
  // 参数送入 w19（icallcc 约定），跳板据此解码查表。调用约定 icallcc：
  // 参数/返回沿用标准 ABI 位置，后端 CSR 掩码把 x19 视为调用方保存。
  IRBuilder<> B(OldCall);

  // 参与加密的参数先过正向运算链，再送进 icallcc 跳板。
  SmallVector<Value *, 8> Args;
  for (unsigned I = 0; I < OldCall->arg_size(); ++I)
    Args.push_back(OldCall->getArgOperand(I));
  Args.push_back(ConstantInt::get(Type::getInt32Ty(OldCall->getContext()),
                                  PackedIndex));
  CallInst *NewCall =
      CallInst::Create(&Trampoline, Args, OldCall->getName(),
                       OldCall->getIterator());
  NewCall->setCallingConv(CallingConv::ICall);
  AttributeList OldAttrs = OldCall->getAttributes();
  SmallVector<AttributeSet, 8> ParamAttrs;
  for (unsigned I = 0; I < OldCall->arg_size(); ++I)
    ParamAttrs.push_back(OldAttrs.getParamAttrs(I));
  ParamAttrs.push_back(AttributeSet::get(
      OldCall->getContext(),
      {Attribute::get(OldCall->getContext(), Attribute::Nest)}));
  NewCall->setAttributes(AttributeList::get(OldCall->getContext(),
                                            AttributeSet(),
                                            OldAttrs.getRetAttrs(),
                                            ParamAttrs));
  NewCall->setDebugLoc(OldCall->getDebugLoc());
  NewCall->copyMetadata(*OldCall);
  if (!OldCall->getType()->isVoidTy())
    OldCall->replaceAllUsesWith(NewCall);
  OldCall->eraseFromParent();
}

// 每个改写调用点独有的跳板：naked 函数，签名与被调函数一致。函数体为
// 按目标随机生成的汇编：先在 x0~x7 上就地应用参数正向运算链（常量经
// 字面池 ldr 读入 x9/w9），再以编译期常量 (group_id, index) 查池，经
// ldr x19 / ret x19 跳转到真实目标（目标入口做逆运算）。naked 保证不
// 动 lr，目标 ret 直接返回原始调用者；x19 由目标按被调约定保存恢复。
// runtime 属性防止跳板被内联或优化改写。
Function *createCallTrampoline(Function &Target,
                               ArrayRef<ArgCryptPlan> Plans,
                               GlobalVariable *PoolData, StringRef Suffix,
                               unsigned Site) {
  Module &M = *Target.getParent();
  LLVMContext &Ctx = M.getContext();
  // 签名 = 目标签名 + 尾部一个 i32 nest：调用号（group_id<<16|index）
  // 由调用点常量经 icallcc 送入 w19（后端按 CCIfNest 分配 W19），
  // 跳板运行期从 w19 解码查表。其余参数/返回与目标完全一致。
  SmallVector<Type *, 8> Params;
  for (unsigned I = 0; I < Target.arg_size(); ++I)
    Params.push_back(Target.getArg(I)->getType());
  Params.push_back(Type::getInt32Ty(Ctx));
  Function *Tramp = Function::Create(
      FunctionType::get(Target.getReturnType(), Params, false),
      GlobalValue::InternalLinkage,
      (Twine("__vllvm_icall.") + Suffix + ".call." + Twine(Site) + "." +
       Target.getName())
          .str(),
      M);
  addRuntimeAttrs(*Tramp);
  Tramp->addFnAttr(Attribute::Naked);
  // icallcc：参数/返回沿用标准 ABI 位置（nest i32 进 w19），但后端 CSR
  // 掩码把 x19 视为调用方保存——ret x19 查表跳转会破坏 x19，调用方的
  // 寄存器分配必须知情。
  Tramp->setCallingConv(CallingConv::ICall);
  Tramp->addParamAttr(Tramp->arg_size() - 1, Attribute::Nest);

  Triple TT(M.getTargetTriple());
  std::string Sym = PoolData->getName().str();
  if (TT.isOSBinFormatMachO())
    Sym = "_" + Sym;

  std::string Asm;
  for (const ArgCryptPlan &Plan : Plans) {
    bool Is64 = Plan.Width == 64;
    std::string R = (Is64 ? "x" : "w") + std::to_string(Plan.Reg);
    std::string SCR = Is64 ? "x9" : "w9";
    for (const CryptOp &Op : Plan.Ops) {
      switch (Op.K) {
      case CryptOp::Add:
        Asm += "ldr " + SCR + ", =0x" + utohexstr(Is64 ? Op.C : (uint32_t)Op.C) +
               "\nadd " + R + ", " + R + ", " + SCR + "\n";
        break;
      case CryptOp::Sub:
        Asm += "ldr " + SCR + ", =0x" + utohexstr(Is64 ? Op.C : (uint32_t)Op.C) +
               "\nsub " + R + ", " + R + ", " + SCR + "\n";
        break;
      case CryptOp::Xor:
        Asm += "ldr " + SCR + ", =0x" + utohexstr(Is64 ? Op.C : (uint32_t)Op.C) +
               "\neor " + R + ", " + R + ", " + SCR + "\n";
        break;
      }
    }
  }
  // 查表跳转（与 src/funccaller 的 call_func 一致）：从 w19 解码
  // group_id/index，group 结构 {i32, ptr} 长 16 字节、func_array 在 +8，
  // 槽位宽 8 字节，取目标地址后 ret x19。
  if (TT.isOSBinFormatMachO())
    Asm += "adrp x9, " + Sym + "@PAGE\nldr x9, [x9, " + Sym + "@PAGEOFF]\n";
  else
    Asm += "adrp x9, " + Sym + "\nldr x9, [x9, :lo12:" + Sym + "]\n";
  Asm += "ubfx w16, w19, #16, #8\n";
  Asm += "and w17, w19, #0xff\n";
  Asm += "add x9, x9, x16, lsl #4\n";
  Asm += "ldr x9, [x9, #8]\n";
  Asm += "ldr x19, [x9, x17, lsl #3]\n";
  Asm += "ret x19\n";

  IRBuilder<> IRB(BasicBlock::Create(Ctx, "entry", Tramp));
  FunctionType *AsmTy = FunctionType::get(Type::getVoidTy(Ctx), false);
  InlineAsm *Thunk = InlineAsm::get(AsmTy, Asm, "", true);
  IRB.CreateCall(AsmTy, Thunk);
  IRB.CreateUnreachable();
  return Tramp;
}
} // namespace

PreservedAnalyses IndirectCallPass::run(Module &M,
                                        ModuleAnalysisManager &MAM) {
  (void)MAM;
  llvm::vllvm::VLLVMConfig &Config = llvm::vllvm::VLLVMConfig::get();
  // icall 等级（Pass 自定语义）：1=只间接调用；2=再混入参数加密。
  Config.registerPassLevels("icall", 1, 2);
  unsigned Level = Config.getLevel("icall");
  if (Level == 0)
    return PreservedAnalyses::all();
  bool UseCrypt = Level >= 2;
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
  // 目标还存在间接调用，这些目标一律不加密。非 local linkage 的目标
  // 还可能被其他编译单元明文直调（跨模块调用点对本 module pass 不可
  // 见，无法改写），同样保持明文，只入池不做参数加密。
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
        continue; // 该调用点会被改写为专属跳板调用并加密
      CryptBlocked.insert(Callee);
    }
  }
  DenseMap<Function *, std::vector<ArgCryptPlan>> CryptPlans;
  if (UseCrypt) {
    for (Function *Target : Targets) {
      if (CryptBlocked.count(Target) || isFunctionAddressTaken(*Target))
        continue;
      if (!Target->hasLocalLinkage())
        continue; // 跨模块可直调的目标不加密，避免明文实文命中解密入口
      std::vector<ArgCryptPlan> Plans = buildArgCryptPlans(*Target, Engine);
      if (!Plans.empty()) {
        CryptPlans.try_emplace(Target, std::move(Plans));
        Target->addFnAttr(CryptAttr);
      }
    }
  }

  ICallRuntime Runtime =
      createRuntime(M, GroupCount, GroupLength, Suffix);
  // 第三步：在 init_array 中注册所有目标，池槽直接登记原函数地址。
  createRegistrationCtor(M, Runtime, Targets, Locations, GroupCount,
                         GroupLength, Suffix);

  // 第四步：为每个改写调用点生成专属跳板，并把原调用替换为对跳板的
  // 普通直调（明文实参）。跳板函数签名与目标一致，内部完成可选的正向
  // 参数加密与 (group_id, index) 查池，再间接尾调用目标；index 只在
  // 编译期作为跳板内常量存在。
  unsigned SiteCounter = 0;
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
      Function *Tramp = createCallTrampoline(
          *P.Callee,
          It == CryptPlans.end() ? ArrayRef<ArgCryptPlan>()
                                 : ArrayRef(It->second),
          Runtime.PoolData, Suffix, SiteCounter++);
      rewriteCall(*Tramp, P.Packed, P.Call);
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
