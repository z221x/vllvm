#include "VmpPass.h"

#include "VLLVMAttribute.h"
#include "VmpCommon.h"
#include "VmpFunctionCompiler.h"
#include "config/VLLVMConfig.h"
#include "VmpRuntimeEmbed.h"

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/Analysis/OptimizationRemarkEmitter.h"
#include "llvm/Bitcode/BitcodeReader.h"
#include "llvm/IR/Attributes.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/GetElementPtrTypeIterator.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/IR/LegacyPassManager.h"
#include "llvm/IR/Module.h"
#include "llvm/Linker/Linker.h"
#include "llvm/Support/Alignment.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/Support/MemoryBufferRef.h"
#include "llvm/TargetParser/Triple.h"
#include "llvm/Transforms/Utils.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"
#include "llvm/Transforms/Utils/Cloning.h"
#include "llvm/Transforms/Utils/LowerSwitch.h"
#include "llvm/Transforms/Utils/ModuleUtils.h"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <memory>
#include <string>
#include <utility>
#include <vector>

using namespace llvm;
using ::vllvm::vm::kFrameAlignment;
using ::vllvm::vm::kInstructionSize;
using ::vllvm::vm::kMaxArgumentCount;
using ::vllvm::vm::kMaxFrameSize;

namespace llvm::vllvm {
namespace {

constexpr StringLiteral VmpProcessedAttr = "vllvm.vmp.processed";
constexpr StringLiteral VmpInjectedNoInlineAttr = "vllvm.vmp.injected.noinline";
constexpr StringLiteral VmpHadAlwaysInlineAttr = "vllvm.vmp.had.alwaysinline";
constexpr StringLiteral RuntimeName = "__vllvm_vmp_execute";
constexpr StringLiteral RuntimeHostBridgeName = "__vllvm_vmp_hostcall_bridge";
constexpr StringLiteral RuntimeAttr = "vllvm.vmp.runtime";

struct CompiledFunction {
  Function *Source = nullptr;
  std::vector<std::uint64_t> Code;
  std::vector<std::uint64_t> Constants;
  // 值表里需要包装函数运行期回填全局地址的槽位（名字对应原 Module）。
  std::vector<VMPCodegenResult::AddressSlot> AddressSlots;
  struct HostCallTarget {
    Function *Target = nullptr;
    std::string PseudoSymbol;
    std::uint32_t ArgumentCount = 0;
  };
  std::vector<HostCallTarget> HostCalls;
  std::uint32_t FrameSize = 0;
};

[[nodiscard]] bool isScalarVmType(Type *Ty) {
  if (Ty->isPointerTy())
    return Ty->getPointerAddressSpace() == 0;
  auto *ITy = dyn_cast<IntegerType>(Ty);
  return ITy && (ITy->getBitWidth() == 1 || ITy->getBitWidth() == 8 ||
                 ITy->getBitWidth() == 16 || ITy->getBitWidth() == 32 ||
                 ITy->getBitWidth() == 64);
}

// 忽略常规的调试指令、lifetime、assume 等 LLVM intrinsic
[[nodiscard]] bool isIgnorableIntrinsic(const Instruction &I) {
  const auto *II = dyn_cast<IntrinsicInst>(&I);
  if (!II)
    return false;
  switch (II->getIntrinsicID()) {
  case Intrinsic::dbg_declare:
  case Intrinsic::dbg_value:
  case Intrinsic::dbg_assign:
  case Intrinsic::dbg_label:
  case Intrinsic::lifetime_start:
  case Intrinsic::lifetime_end:
  case Intrinsic::assume:
    return true;
  default:
    return false;
  }
}

[[nodiscard]] std::string checkFastCallNormalization(const Function &Target) {
  if (Target.getCallingConv() == CallingConv::C)
    return {};
  if (Target.getCallingConv() != CallingConv::Fast)
    return "M3 HOSTCALL 只支持 C 或可安全规范化的 fastcc 调用";
  if (Target.isDeclaration() || !Target.hasLocalLinkage())
    return "fastcc HOSTCALL 目标必须是当前 Module 的内部定义";
  if (Target.hasAddressTaken())
    return "fastcc HOSTCALL 目标地址已逃逸，不能安全改为 C 调用约定";
  for (const User *U : Target.users()) {
    const auto *Call = dyn_cast<CallBase>(U);
    if (!Call || Call->getCalledOperand()->stripPointerCasts() != &Target)
      return "fastcc HOSTCALL 目标存在非直接调用用途";
    if (Call->getCallingConv() != CallingConv::Fast)
      return "fastcc HOSTCALL 目标存在调用约定不一致的调用点";
    if (Call->isMustTailCall())
      return "fastcc HOSTCALL 目标存在不能改写的 musttail 调用";
  }
  return {};
}

[[nodiscard]] std::string checkHostCall(const CallInst &Call) {
  if (Call.isMustTailCall())
    return "M3 HOSTCALL 不支持 musttail";
  if (Call.hasOperandBundles())
    return "M3 HOSTCALL 不支持 operand bundle";
  auto *Target =
      dyn_cast<Function>(Call.getCalledOperand()->stripPointerCasts());
  if (!Target)
    return "M3 HOSTCALL 只支持可静态解析的直接调用";
  if (Target->isIntrinsic())
    return "包含不能移除或执行的 LLVM intrinsic";
  if (Call.getCallingConv() != Target->getCallingConv())
    return "HOSTCALL 调用点与目标函数的调用约定不一致";
  if (Target->getCallingConv() != CallingConv::C &&
      Target->getCallingConv() != CallingConv::Fast)
    return "M3 HOSTCALL 只支持 C 或优化生成的 fastcc 调用";
  if (std::string Reason = checkFastCallNormalization(*Target); !Reason.empty())
    return Reason;
  if (Target->isVarArg())
    return "M3 HOSTCALL 不支持可变参数目标";
  if (Call.arg_size() != Target->arg_size() ||
      Call.getFunctionType() != Target->getFunctionType())
    return "HOSTCALL 调用点类型与直接目标函数类型不一致";
  if (Call.arg_size() > ::vllvm::vm::kMaxHostCallArgumentCount)
    return "HOSTCALL 参数数量超过统一 bridge 的 15 参数上限";
  if (!Call.getType()->isVoidTy() && !isScalarVmType(Call.getType()))
    return "HOSTCALL 返回类型不是整数、指针或 void";
  for (unsigned I = 0; I != Call.arg_size(); ++I) {
    if (!isScalarVmType(Call.getArgOperand(I)->getType()))
      return "HOSTCALL 参数包含浮点、聚合或非零地址空间指针";
    if (Call.paramHasAttr(I, Attribute::ByVal) ||
        Call.paramHasAttr(I, Attribute::StructRet) ||
        Call.paramHasAttr(I, Attribute::InAlloca) ||
        Call.paramHasAttr(I, Attribute::InReg) ||
        Call.paramHasAttr(I, Attribute::Nest) ||
        Call.paramHasAttr(I, Attribute::SwiftSelf) ||
        Call.paramHasAttr(I, Attribute::SwiftError))
      return "HOSTCALL 参数包含统一 bridge 不支持的特殊 ABI 属性";
  }
  return {};
}

[[nodiscard]] std::string checkEligibility(Function &F) {
  if (F.isDeclaration())
    return "函数没有可虚拟化的定义";
  if (F.isVarArg())
    return "暂不支持可变参数函数";
  if (F.getCallingConv() != CallingConv::C)
    return "暂只支持默认 C 调用约定";
  if (F.arg_size() > kMaxArgumentCount)
    return "参数数量超过 R0-R5 的 M3 上限";
  if (!F.getReturnType()->isVoidTy() && !isScalarVmType(F.getReturnType()))
    return "返回类型不是 M3 标量类型";
  for (Argument &Arg : F.args()) {
    if (!isScalarVmType(Arg.getType()))
      return "参数包含聚合、浮点或非零地址空间指针";
    if (Arg.hasByValAttr() || Arg.hasStructRetAttr() || Arg.hasInAllocaAttr())
      return "参数包含 byval/sret/inalloca ABI 属性";
  }
  if (F.hasFnAttribute(Attribute::Naked))
    return "naked 函数不能生成宿主包装函数";

  for (Instruction &I : instructions(F)) {
    if (isIgnorableIntrinsic(I))
      continue;
    if (auto *BO = dyn_cast<BinaryOperator>(&I)) {
      switch (BO->getOpcode()) {
      case Instruction::Add:
      case Instruction::Sub:
      case Instruction::Mul:
      case Instruction::UDiv:
      case Instruction::SDiv:
      case Instruction::URem:
      case Instruction::SRem:
      case Instruction::And:
      case Instruction::Or:
      case Instruction::Xor:
      case Instruction::Shl:
      case Instruction::LShr:
      case Instruction::AShr:
        if (isScalarVmType(BO->getType()) && BO->getType()->isIntegerTy())
          continue;
        break;
      default:
        break;
      }
      return "包含不支持的二元运算";
    }
    if (auto *AI = dyn_cast<AllocaInst>(&I)) {
      // 数组/标量静态 alloca 均可：VM 帧按 DataLayout 字节布局分配
      //（Direct CodeGen 的 layoutFrame/allocaSize 支持任意定长类型），
      // 访问仍受下方标量 load/store 检查约束。
      if (!AI->isStaticAlloca() || !AI->getAllocatedType()->isSized() ||
          AI->getAlign().value() > kFrameAlignment)
        return "只支持静态定长（含标量/数组）、对齐不超过 16 字节的 alloca";
      continue;
    }
    if (auto *LI = dyn_cast<LoadInst>(&I)) {
      // volatile 在 VM 中按程序序逐次真实访存执行，单线程语义等价；
      // atomic 仍拒绝（VM 不提供原子性与跨线程顺序保证）。
      if (LI->isAtomic() || !isScalarVmType(LI->getType()))
        return "load 必须是非 atomic 的 M3 标量访问";
      continue;
    }
    if (auto *SI = dyn_cast<StoreInst>(&I)) {
      if (SI->isAtomic() ||
          !isScalarVmType(SI->getValueOperand()->getType()))
        return "store 必须是非 atomic 的 M3 标量访问";
      continue;
    }
    if (auto *Cmp = dyn_cast<ICmpInst>(&I)) {
      if (isScalarVmType(Cmp->getOperand(0)->getType()))
        continue;
      return "icmp 只能比较受支持的整数或普通指针";
    }
    if (auto *Phi = dyn_cast<PHINode>(&I)) {
      if (isScalarVmType(Phi->getType()))
        continue;
      return "PHI 只能承载受支持的整数或普通指针";
    }
    if (isa<BranchInst, SwitchInst, SelectInst, ReturnInst, UnreachableInst>(
            &I))
      continue;
    if (auto *CI = dyn_cast<CastInst>(&I)) {
      if (isa<TruncInst, ZExtInst, SExtInst, BitCastInst, PtrToIntInst,
              IntToPtrInst>(CI) &&
          isScalarVmType(CI->getSrcTy()) && isScalarVmType(CI->getDestTy()))
        continue;
      return "只支持 trunc/zext/sext/ptrtoint/inttoptr/等宽 bitcast";
    }
    // 正常流程下 GEP 已被 lowerGEPs 分解；这里只兜底不可分解的残留
    //（如 PHI incoming 里的常量表达式 GEP 未物化）。
    if (isa<GetElementPtrInst>(&I))
      return "GEP 分解后仍残留 getelementptr（不可物化的指针常量表达式）";
    if (auto *Call = dyn_cast<CallInst>(&I)) {
      std::string Reason = checkHostCall(*Call);
      if (Reason.empty())
        continue;
      return Reason;
    }
    if (isa<CallBase>(&I))
      return "M3 HOSTCALL 暂不支持 invoke/callbr";
    return (Twine("包含不支持的 IR 指令: ") + I.getOpcodeName()).str();
  }
  return {};
}

void lowerSelects(Function &F) {
  SmallVector<SelectInst *, 16> Selects;
  for (Instruction &I : instructions(F))
    if (auto *SI = dyn_cast<SelectInst>(&I))
      Selects.push_back(SI);

  for (SelectInst *SI : Selects) {
    Instruction *ThenTerm = nullptr;
    Instruction *ElseTerm = nullptr;
    Value *TrueValue = SI->getTrueValue();
    Value *FalseValue = SI->getFalseValue();
    SplitBlockAndInsertIfThenElse(SI->getCondition(), SI, &ThenTerm, &ElseTerm,
                                  SI->getMetadata(LLVMContext::MD_prof));
    auto *Phi = PHINode::Create(SI->getType(), 2, SI->getName() + ".vmp",
                                SI->getIterator());
    Phi->addIncoming(TrueValue, ThenTerm->getParent());
    Phi->addIncoming(FalseValue, ElseTerm->getParent());
    SI->replaceAllUsesWith(Phi);
    SI->eraseFromParent();
  }
}

void lowerSwitches(Function &F) {
  legacy::FunctionPassManager FPM(F.getParent());
  FPM.add(createLowerSwitchPass());
  FPM.doInitialization();
  FPM.run(F);
  FPM.doFinalization();
}

// 把指令操作数里的 ConstantExpr 物化成真实指令：IRBuilder 常量折叠会
// 把 ptrtoint(@g) 等折回 ConstantExpr，而 Direct CodeGen 只能对指令取
// 值（纯整数常量表达式物化后同样无害）。PHI 的 incoming 物化到对应
// 前驱块终结指令之前；物化出的 GEP 由 lowerGEPs 在下一轮分解。
bool materializeConstantExprs(Function &F) {
  bool Changed = false;
  bool Progress = true;
  while (Progress) {
    Progress = false;
    for (BasicBlock &BB : F) {
      for (auto It = BB.begin(), E = BB.end(); It != E;) {
        Instruction *User = &*It++;
        if (auto *Phi = dyn_cast<PHINode>(User)) {
          for (unsigned In = 0; In < Phi->getNumIncomingValues(); ++In) {
            auto *CE = dyn_cast<ConstantExpr>(Phi->getIncomingValue(In));
            if (!CE)
              continue;
          Instruction *Expanded = CE->getAsInstruction();
          Expanded->insertBefore(Phi->getIncomingBlock(In)->getTerminator());
          Phi->setIncomingValue(In, Expanded);
          Changed = Progress = true;
          }
          continue;
        }
        for (unsigned Op = 0; Op < User->getNumOperands(); ++Op) {
          auto *CE = dyn_cast<ConstantExpr>(User->getOperand(Op));
          if (!CE)
            continue;
          Instruction *Expanded = CE->getAsInstruction();
          Expanded->insertBefore(User);
          User->setOperand(Op, Expanded);
          Changed = Progress = true;
        }
      }
    }
  }
  return Changed;
}

// 把 memcpy/memmove/memset intrinsic 改写为对同名 libc 函数的直接
// 调用，交给 HOSTCALL 机制执行（参数均为标量指针/长度）；llvm.objectsize
// 折叠为 -1（未知大小）：-O0 下 fortify 的 __memcpy/__memset_chk 以它
// 作 dstlen，未知大小按约定跳过长度检查。同时在原 Module 补 libc 声明，
// 保证 lowerHostCalls 的按名回查可用。
void lowerMemIntrinsics(Function &F, Module &OriginalModule) {
  Module *M = F.getParent();
  // 先收集再改写：遍历中 erase 会留下悬空迭代器。
  SmallVector<CallInst *, 8> MemCalls;
  SmallVector<CallInst *, 8> ObjectSizes;
  for (Instruction &I : instructions(F)) {
    auto *Call = dyn_cast<CallInst>(&I);
    if (!Call)
      continue;
    Function *Callee = Call->getCalledFunction();
    if (!Callee)
      continue;
    const Intrinsic::ID ID = Callee->getIntrinsicID();
    if (ID == Intrinsic::memcpy || ID == Intrinsic::memmove ||
        ID == Intrinsic::memset)
      MemCalls.push_back(Call);
    else if (ID == Intrinsic::objectsize)
      ObjectSizes.push_back(Call);
  }
  for (CallInst *Call : ObjectSizes) {
    Call->replaceAllUsesWith(ConstantInt::get(
        Type::getInt64Ty(F.getContext()),
        static_cast<uint64_t>(-1), true));
    Call->eraseFromParent();
  }
  for (CallInst *Call : MemCalls) {
    const Intrinsic::ID ID = Call->getCalledFunction()->getIntrinsicID();
    const bool IsSet = ID == Intrinsic::memset;
    Type *PtrTy = PointerType::getUnqual(F.getContext());
    Type *I64Ty = Type::getInt64Ty(F.getContext());
    FunctionType *FnTy = FunctionType::get(
        PtrTy, {PtrTy, IsSet ? Type::getInt32Ty(F.getContext()) : PtrTy, I64Ty},
        false);
    const char *Name = IsSet ? "memset" : (ID == Intrinsic::memcpy ? "memcpy"
                                                                   : "memmove");
    OriginalModule.getOrInsertFunction(Name, FnTy);
    FunctionCallee Libc = M->getOrInsertFunction(Name, FnTy);

    IRBuilder<> B(Call);
    SmallVector<Value *, 3> Args;
    for (unsigned A = 0; A < 3; ++A)
      Args.push_back(Call->getArgOperand(A));
    if (IsSet)
      Args[1] = B.CreateZExt(Args[1], Type::getInt32Ty(F.getContext()));
    B.CreateCall(Libc, Args);
    Call->eraseFromParent();
  }
}

// GEP → 整数地址运算：base = ptrtoint；顺序下标按元素大小缩放相加
//（常量下标折叠进常量偏移），结构体下标按 DataLayout 字段偏移折叠，
// 动态下标按有符号语义 sext 到 i64，结果 inttoptr 回指针。全程只用
// M3 已支持的原语（ADD/MUL/SEXT/BITCAST），不扩展 ISA。
bool lowerGEPs(Function &F) {
  const DataLayout &DL = F.getParent()->getDataLayout();
  SmallVector<GetElementPtrInst *, 32> GEPs;
  for (Instruction &I : instructions(F))
    if (auto *GEP = dyn_cast<GetElementPtrInst>(&I))
      GEPs.push_back(GEP);
  if (GEPs.empty())
    return false;

  Type *I64 = Type::getInt64Ty(F.getContext());
  for (GetElementPtrInst *GEP : GEPs) {
    IRBuilder<> B(GEP);
    APInt ConstOffset(64, 0, true);
    SmallVector<std::pair<Value *, std::uint64_t>, 4> Dynamic;
    bool Failed = false;
    for (gep_type_iterator GTI = gep_type_begin(GEP),
                           GTE = gep_type_end(GEP);
         GTI != GTE && !Failed; ++GTI) {
      Value *Index = GTI.getOperand();
      if (StructType *ST = GTI.getStructTypeOrNull()) {
        auto *FieldNo = dyn_cast<ConstantInt>(Index);
        if (!FieldNo) {
          Failed = true; // 结构体字段下标必须是常量（合法 IR 保证）
          break;
        }
        ConstOffset += APInt(
            64, DL.getStructLayout(ST)->getElementOffset(FieldNo->getZExtValue()),
            true);
      } else {
        Type *ElemTy = GTI.getIndexedType();
        if (!ElemTy->isSized()) {
          Failed = true;
          break;
        }
        TypeSize Scale = DL.getTypeAllocSize(ElemTy);
        if (Scale.isScalable()) {
          Failed = true;
          break;
        }
        const std::uint64_t ScaleV = Scale.getFixedValue();
        if (auto *C = dyn_cast<ConstantInt>(Index)) {
          ConstOffset += C->getValue().sext(64) * APInt(64, ScaleV, true);
        } else {
          Dynamic.emplace_back(Index, ScaleV);
        }
      }
    }
    if (Failed)
      continue; // 保留原 GEP，由 checkEligibility 拒绝并回退

    Value *Addr = B.CreatePtrToInt(GEP->getPointerOperand(), I64,
                                   GEP->getName() + ".p2i");
    if (!ConstOffset.isZero())
      Addr = B.CreateAdd(Addr, ConstantInt::get(I64, ConstOffset),
                         GEP->getName() + ".off");
    for (auto &[Index, ScaleV] : Dynamic) {
      Value *Idx64 =
          Index->getType() == I64 ? Index : B.CreateSExt(Index, I64);
      Value *Scaled =
          ScaleV == 1 ? Idx64
                      : B.CreateMul(Idx64, ConstantInt::get(I64, ScaleV));
      Addr = B.CreateAdd(Addr, Scaled, GEP->getName() + ".idx");
    }
    Instruction *Ptr = cast<Instruction>(
        B.CreateIntToPtr(Addr, GEP->getType(), GEP->getName() + ".ptr"));
    GEP->replaceAllUsesWith(Ptr);
    GEP->eraseFromParent();
  }
  return true;
}

Value *packVmScalar(IRBuilder<> &Builder, Value *ValueToPack) {
  Type *Ty = ValueToPack->getType();
  Type *I64 = Builder.getInt64Ty();
  if (Ty->isPointerTy())
    return Builder.CreatePtrToInt(ValueToPack, I64);
  return Builder.CreateZExtOrTrunc(ValueToPack, I64);
}

Value *packHostCallScalar(IRBuilder<> &Builder, const CallInst &Call,
                          unsigned ArgumentIndex) {
  Value *ValueToPack = Call.getArgOperand(ArgumentIndex);
  Type *Ty = ValueToPack->getType();
  Type *I64 = Builder.getInt64Ty();
  if (Ty->isPointerTy())
    return Builder.CreatePtrToInt(ValueToPack, I64);
  if (Call.paramHasAttr(ArgumentIndex, Attribute::SExt))
    return Builder.CreateSExtOrTrunc(ValueToPack, I64);
  return Builder.CreateZExtOrTrunc(ValueToPack, I64);
}

Value *unpackVmScalar(IRBuilder<> &Builder, Value *Packed, Type *Ty) {
  if (Ty->isPointerTy())
    return Builder.CreateIntToPtr(Packed, Ty);
  return Builder.CreateTruncOrBitCast(Packed, Ty);
}

bool lowerHostCalls(Function &F, Module &OriginalModule,
                    std::vector<CompiledFunction::HostCallTarget> &Targets,
                    std::string &Error) {
  SmallVector<CallInst *, 16> Calls;
  DenseMap<Function *, std::uint32_t> TargetIndices;
  std::uint32_t MaxOverflowArguments = 0;

  for (Instruction &I : instructions(F)) {
    auto *Call = dyn_cast<CallInst>(&I);
    if (!Call)
      continue;
    auto *PreparedTarget =
        dyn_cast<Function>(Call->getCalledOperand()->stripPointerCasts());
    if (!PreparedTarget) {
      Error = "HOSTCALL lowering 遇到非直接调用";
      return false;
    }
    Function *OriginalTarget =
        OriginalModule.getFunction(PreparedTarget->getName());
    if (!OriginalTarget) {
      Error = "无法在原 Module 中解析 HOSTCALL 目标 " +
              PreparedTarget->getName().str();
      return false;
    }
    if (std::string Reason = checkFastCallNormalization(*OriginalTarget);
        !Reason.empty()) {
      Error = Reason;
      return false;
    }
    auto [It, Inserted] =
        TargetIndices.try_emplace(OriginalTarget, Targets.size());
    if (Inserted) {
      if (Targets.size() > ::vllvm::vm::kHostCallIndexMask) {
        Error = "HOSTCALL 目标数量超过 Payload 的 24 位 index 上限";
        return false;
      }
      Targets.push_back({OriginalTarget,
                         "__vllvm_vmp_hostcall." + utostr(Targets.size()) +
                             "." + utostr(Call->arg_size()),
                         static_cast<std::uint32_t>(Call->arg_size())});
    } else if (Targets[It->second].ArgumentCount != Call->arg_size()) {
      Error = "同一 HOSTCALL 目标使用了不一致的参数数量";
      return false;
    }
    Calls.push_back(Call);
    if (Call->arg_size() > kMaxArgumentCount)
      MaxOverflowArguments = std::max<std::uint32_t>(
          MaxOverflowArguments, Call->arg_size() - kMaxArgumentCount);
  }

  if (Calls.empty())
    return true;

  LLVMContext &Context = F.getContext();
  Type *I8 = Type::getInt8Ty(Context);
  Type *I64 = Type::getInt64Ty(Context);
  const std::uint32_t MaxOverflowBytes = ::vllvm::vm::hostCallOverflowSize(
      kMaxArgumentCount + MaxOverflowArguments);
  AllocaInst *OutgoingArguments = nullptr;
  ArrayType *OutgoingType = nullptr;
  if (MaxOverflowBytes != 0) {
    OutgoingType = ArrayType::get(I8, MaxOverflowBytes);
    IRBuilder<> EntryBuilder(&*F.getEntryBlock().getFirstInsertionPt());
    OutgoingArguments = EntryBuilder.CreateAlloca(OutgoingType, nullptr,
                                                  "vmp.hostcall.stack.args");
    OutgoingArguments->setAlignment(Align(kFrameAlignment));
    // LLVM 可在嵌套 CodeGen 前重命名或丢弃 SSA 名称；使用 metadata 稳定标识
    // 必须固定在 frame 尾部的 HOSTCALL outgoing 区。
    OutgoingArguments->setMetadata(
        "vllvm.vmp.hostcall.outgoing",
        MDNode::get(Context, ArrayRef<Metadata *>()));
  }

  SmallVector<Function *, 16> PseudoFunctions(Targets.size(), nullptr);
  for (std::uint32_t Index = 0; Index != Targets.size(); ++Index) {
    const unsigned RegisterArguments = std::min<std::uint32_t>(
        Targets[Index].ArgumentCount, kMaxArgumentCount);
    SmallVector<Type *, kMaxArgumentCount> Parameters(RegisterArguments, I64);
    FunctionType *PseudoType = FunctionType::get(I64, Parameters, false);
    PseudoFunctions[Index] =
        Function::Create(PseudoType, GlobalValue::ExternalLinkage,
                         Targets[Index].PseudoSymbol, F.getParent());
  }

  for (CallInst *Call : Calls) {
    auto *PreparedTarget =
        cast<Function>(Call->getCalledOperand()->stripPointerCasts());
    Function *OriginalTarget =
        OriginalModule.getFunction(PreparedTarget->getName());
    const std::uint32_t Index = TargetIndices.lookup(OriginalTarget);
    IRBuilder<> Builder(Call);
    SmallVector<Value *, kMaxArgumentCount> RegisterArguments;
    for (unsigned I = 0;
         I != std::min<std::uint32_t>(Call->arg_size(), kMaxArgumentCount); ++I)
      RegisterArguments.push_back(packHostCallScalar(Builder, *Call, I));

    if (Call->arg_size() > kMaxArgumentCount) {
      const std::uint32_t CurrentBytes =
          ::vllvm::vm::hostCallOverflowSize(Call->arg_size());
      const std::uint32_t BaseByte = MaxOverflowBytes - CurrentBytes;
      std::uint32_t NativeStackOffset = 2 * sizeof(std::uint64_t);
      const bool UsesApplePackedStack =
          Triple(OriginalModule.getTargetTriple()).isOSDarwin();
      for (std::uint32_t I = kMaxArgumentCount; I != Call->arg_size(); ++I) {
        std::uint32_t Offset = 0;
        unsigned StoreBytes = sizeof(std::uint64_t);
        if (I < 8) {
          Offset = (I - kMaxArgumentCount) * sizeof(std::uint64_t);
        } else if (UsesApplePackedStack) {
          Type *ArgumentType = Call->getArgOperand(I)->getType();
          StoreBytes =
              ArgumentType->isPointerTy()
                  ? 8
                  : std::max(
                        1U, cast<IntegerType>(ArgumentType)->getBitWidth() / 8);
          NativeStackOffset = alignTo(NativeStackOffset, StoreBytes);
          Offset = NativeStackOffset;
          NativeStackOffset += StoreBytes;
        } else {
          Offset = (I - kMaxArgumentCount) * sizeof(std::uint64_t);
        }
        if (Offset + StoreBytes > CurrentBytes) {
          Error = "HOSTCALL 宿主栈参数布局超过预留 outgoing 区";
          return false;
        }
        Value *Slot = Builder.CreateInBoundsGEP(
            OutgoingType, OutgoingArguments,
            {Builder.getInt32(0), Builder.getInt32(BaseByte + Offset)});
        Value *Packed = packHostCallScalar(Builder, *Call, I);
        if (StoreBytes != sizeof(std::uint64_t))
          Packed =
              Builder.CreateTrunc(Packed, Builder.getIntNTy(StoreBytes * 8));
        StoreInst *Store = Builder.CreateStore(Packed, Slot);
        Store->setAlignment(Align(StoreBytes));
        Store->setVolatile(true);
      }
    }

    CallInst *PseudoCall =
        Builder.CreateCall(PseudoFunctions[Index], RegisterArguments);
    PseudoCall->setCallingConv(CallingConv::C);
    if (!Call->getType()->isVoidTy())
      Call->replaceAllUsesWith(
          unpackVmScalar(Builder, PseudoCall, Call->getType()));
    Call->eraseFromParent();
  }
  return true;
}

void emitMissed(Function &F, StringRef Reason) {
  OptimizationRemarkEmitter ORE(&F);
  ORE.emit([&]() {
    return OptimizationRemarkMissed("vmp", "Unsupported", &F)
           << "VMP 保留原生函数：" << ore::NV("Reason", Reason);
  });
}

bool restorePreparationAttributes(Function &F) {
  bool Changed = false;
  if (F.hasFnAttribute(VmpInjectedNoInlineAttr)) {
    F.removeFnAttr(VmpInjectedNoInlineAttr);
    F.removeFnAttr(Attribute::NoInline);
    Changed = true;
  }
  if (F.hasFnAttribute(VmpHadAlwaysInlineAttr)) {
    F.removeFnAttr(VmpHadAlwaysInlineAttr);
    F.addFnAttr(Attribute::AlwaysInline);
    Changed = true;
  }
  return Changed;
}

void normalizeFastHostCallTargets(ArrayRef<CompiledFunction> Functions) {
  DenseMap<Function *, bool> Normalized;
  for (const CompiledFunction &Compiled : Functions) {
    for (const CompiledFunction::HostCallTarget &HostCall :
         Compiled.HostCalls) {
      Function *Target = HostCall.Target;
      if (!Target || Target->getCallingConv() != CallingConv::Fast ||
          !Normalized.try_emplace(Target, true).second)
        continue;
      SmallVector<CallBase *, 8> CallSites;
      for (User *U : Target->users())
        CallSites.push_back(cast<CallBase>(U));
      Target->setCallingConv(CallingConv::C);
      for (CallBase *Call : CallSites)
        Call->setCallingConv(CallingConv::C);
    }
  }
}

bool linkRuntime(Module &M, std::string &Error) {
  LLVMContext &Context = M.getContext();
  Type *I32 = Type::getInt32Ty(Context);
  Type *I64 = Type::getInt64Ty(Context);
  Type *Ptr = PointerType::getUnqual(Context);
  FunctionType *ExpectedType = FunctionType::get(
      I64, {Ptr, I32, Ptr, Ptr, I32, Ptr, I32, Ptr, I32}, false);
  FunctionType *ExpectedHostBridgeType =
      FunctionType::get(I64, {Ptr, Ptr, I32, Ptr}, false);
  if (Function *Existing = M.getFunction(RuntimeName)) {
    if (Existing->getFunctionType() != ExpectedType) {
      Error = "模块内已有不兼容的 __vllvm_vmp_execute 符号";
      return false;
    }
    if (!Existing->isDeclaration()) {
      if (!Existing->hasFnAttribute(RuntimeAttr)) {
        Error = "模块内已有非 VLLVM 提供的 __vllvm_vmp_execute 定义";
        return false;
      }
      Function *HostBridge = M.getFunction(RuntimeHostBridgeName);
      if (!HostBridge || HostBridge->isDeclaration() ||
          HostBridge->getFunctionType() != ExpectedHostBridgeType ||
          !HostBridge->hasFnAttribute(RuntimeAttr)) {
        Error = "模块内已有 VMP runtime，但缺少兼容的统一 HOSTCALL bridge";
        return false;
      }
      return true;
    }
  }
  if (Function *Existing = M.getFunction(RuntimeHostBridgeName)) {
    if (Existing->getFunctionType() != ExpectedHostBridgeType ||
        (!Existing->isDeclaration() &&
         !Existing->hasFnAttribute(RuntimeAttr))) {
      Error = "模块内已有不兼容的 __vllvm_vmp_hostcall_bridge 符号";
      return false;
    }
  }

  ArrayRef<std::uint8_t> Bytes = getVmpRuntimeBitcode();
  MemoryBufferRef Buffer(
      StringRef(reinterpret_cast<const char *>(Bytes.data()), Bytes.size()),
      "vmp-runtime.bc");
  Expected<std::unique_ptr<Module>> Parsed =
      parseBitcodeFile(Buffer, M.getContext());
  if (!Parsed) {
    Error = toString(Parsed.takeError());
    return false;
  }
  std::unique_ptr<Module> Runtime = std::move(*Parsed);
  Runtime->setTargetTriple(M.getTargetTriple());
  Runtime->setDataLayout(M.getDataLayout());
  // 嵌入 bitcode 的宿主 SDK 版本不属于目标模块；保留其余 ABI/代码生成标志。
  // 否则 SDK 更新会触发 Clang 后端不支持的嵌套 linker diagnostic。
  if (NamedMDNode *Flags = Runtime->getModuleFlagsMetadata()) {
    SmallVector<MDNode *, 8> KeptFlags;
    for (MDNode *Flag : Flags->operands())
      if (cast<MDString>(Flag->getOperand(1))->getString() != "SDK Version")
        KeptFlags.push_back(Flag);
    Flags->clearOperands();
    for (MDNode *Flag : KeptFlags)
      Flags->addOperand(Flag);
  }
  for (Function &F : *Runtime) {
    F.removeFnAttr("target-cpu");
    F.removeFnAttr("target-features");
    F.removeFnAttr("tune-cpu");
  }
  if (Linker(M).linkInModule(std::move(Runtime))) {
    Error = "无法把 VMP 解释器 bitcode 链入当前模块";
    return false;
  }
  Function *Execute = M.getFunction(RuntimeName);
  if (!Execute || Execute->isDeclaration()) {
    Error = "嵌入运行时缺少 __vllvm_vmp_execute 定义";
    return false;
  }
  Execute->setLinkage(GlobalValue::LinkOnceODRLinkage);
  Execute->setVisibility(GlobalValue::HiddenVisibility);
  Execute->addFnAttr(RuntimeAttr);
  Function *HostBridge = M.getFunction(RuntimeHostBridgeName);
  if (!HostBridge || HostBridge->isDeclaration() ||
      HostBridge->getFunctionType() != ExpectedHostBridgeType) {
    Error = "嵌入运行时缺少统一 __vllvm_vmp_hostcall_bridge 定义";
    return false;
  }
  HostBridge->setLinkage(GlobalValue::LinkOnceODRLinkage);
  HostBridge->setVisibility(GlobalValue::HiddenVisibility);
  HostBridge->addFnAttr(RuntimeAttr);
  const Triple ModuleTriple(M.getTargetTriple());
  if (ModuleTriple.isOSBinFormatELF() || ModuleTriple.isOSBinFormatCOFF()) {
    Execute->setComdat(M.getOrInsertComdat(RuntimeName));
    HostBridge->setComdat(M.getOrInsertComdat(RuntimeHostBridgeName));
  }
  return true;
}

void removeConflictingAttributes(Function &F) {
  F.removeFnAttr(Attribute::AlwaysInline);
  F.removeFnAttr(Attribute::OptimizeNone);
  F.removeFnAttr(Attribute::Memory);
  F.addFnAttr(Attribute::NoInline);
  for (StringRef Kind : {"bcf", "vmfla", "icall", "enstr", "ibr", "vmp"})
    F.removeFnAttr((Twine("vllvm.") + Kind).str());
  F.removeFnAttr(VmpInjectedNoInlineAttr);
  F.removeFnAttr(VmpHadAlwaysInlineAttr);
  F.addFnAttr(VmpProcessedAttr);
}

bool installWrapper(Module &M, CompiledFunction &Compiled, unsigned TableId,
                    std::string &Error) {
  Function &F = *Compiled.Source;
  LLVMContext &Context = M.getContext();
  Function *Execute = M.getFunction(RuntimeName);
  if (!Execute || Execute->isDeclaration()) {
    Error = "安装包装函数时找不到嵌入解释器";
    return false;
  }
  for (const CompiledFunction::HostCallTarget &HostCall : Compiled.HostCalls) {
    if (!HostCall.Target || HostCall.Target->isIntrinsic() ||
        HostCall.Target->isVarArg() ||
        HostCall.Target->arg_size() != HostCall.ArgumentCount) {
      Error = "安装包装函数前 HOSTCALL 目标契约已改变";
      return false;
    }
  }

  Constant *CodeData = ConstantDataArray::get(Context, Compiled.Code);
  auto *Code = new GlobalVariable(
      M, CodeData->getType(), true, GlobalValue::PrivateLinkage, CodeData,
      "__vllvm_vmp_code." + F.getName() + "." + utostr(TableId));
  Code->setUnnamedAddr(GlobalValue::UnnamedAddr::Global);
  Code->setAlignment(Align(kFrameAlignment));

  GlobalVariable *ValueTable = nullptr;
  if (!Compiled.Constants.empty()) {
    Constant *ValueData = ConstantDataArray::get(Context, Compiled.Constants);
    // 含地址槽的值表必须可写：包装函数每次调用前回填全局地址。
    ValueTable = new GlobalVariable(
        M, ValueData->getType(), Compiled.AddressSlots.empty(),
        GlobalValue::PrivateLinkage, ValueData,
        "__vllvm_vmp_values." + F.getName() + "." + utostr(TableId));
    ValueTable->setUnnamedAddr(GlobalValue::UnnamedAddr::Global);
    ValueTable->setAlignment(Align(8));
  }

  GlobalVariable *FunctionTable = nullptr;
  if (!Compiled.HostCalls.empty()) {
    SmallVector<Constant *, 16> Entries;
    Entries.reserve(Compiled.HostCalls.size());
    for (const CompiledFunction::HostCallTarget &HostCall : Compiled.HostCalls)
      Entries.push_back(HostCall.Target);
    ArrayType *FunctionTableType =
        ArrayType::get(PointerType::getUnqual(Context), Entries.size());
    Constant *FunctionTableData =
        ConstantArray::get(FunctionTableType, Entries);
    FunctionTable = new GlobalVariable(
        M, FunctionTableType, true, GlobalValue::PrivateLinkage,
        FunctionTableData,
        "__vllvm_vmp_functions." + F.getName() + "." + utostr(TableId));
    FunctionTable->setUnnamedAddr(GlobalValue::UnnamedAddr::Global);
    FunctionTable->setAlignment(Align(8));
  }

  SmallVector<GlobalValue *, 3> Used{Code};
  if (ValueTable)
    Used.push_back(ValueTable);
  if (FunctionTable)
    Used.push_back(FunctionTable);
  appendToCompilerUsed(M, Used);

  // deleteBody 在把函数转成声明时会把 linkage 重置为 External：静态
  // （internal/private）函数被虚拟化后会以外部符号参与链接，与其他 TU
  // 的同名函数构成 ODR 冲突（缺陷 #7：bzip2 的 bzlib.c static myfeof
  // 对 bzip2.c 外部 myfeas）。wrapper 只被本模块调用，恢复原 linkage
  // 保持与原函数一致的可见性。
  GlobalValue::LinkageTypes OriginalLinkage = F.getLinkage();
  F.deleteBody();
  F.setLinkage(OriginalLinkage);
  removeConflictingAttributes(F);
  BasicBlock *Entry = BasicBlock::Create(Context, "vmp.entry", &F);
  IRBuilder<> Builder(Entry);

  const unsigned ArgumentSlots = std::max<unsigned>(1, F.arg_size());
  ArrayType *ArgumentArrayTy =
      ArrayType::get(Builder.getInt64Ty(), ArgumentSlots);
  AllocaInst *ArgumentArray = Builder.CreateAlloca(ArgumentArrayTy);
  ArgumentArray->setAlignment(Align(8));
  unsigned ArgumentIndex = 0;
  for (Argument &Arg : F.args()) {
    Value *Slot = Builder.CreateInBoundsGEP(
        ArgumentArrayTy, ArgumentArray,
        {Builder.getInt32(0), Builder.getInt32(ArgumentIndex++)});
    Builder.CreateStore(packVmScalar(Builder, &Arg), Slot);
  }

  // VM 栈由解释器按 FrameSize 自行 malloc/free，包装器不再在宿主栈上
  // 分配帧缓冲（递归时不再逐层占用宿主栈）。
  // 地址槽回填：全局地址含链接期重定位，编译期只能占位 0；这里在
  // 每次调用解释器前把真实地址写进值表（多次/并发调用写入同值，安全）。
  for (const VMPCodegenResult::AddressSlot &Slot : Compiled.AddressSlots) {
    GlobalValue *GV = M.getNamedValue(Slot.GlobalName);
    if (!GV) {
      Error = "地址槽对应的全局符号在原 Module 中不存在: " + Slot.GlobalName;
      return false;
    }
    Value *SlotAddress = Builder.CreateInBoundsGEP(
        ValueTable->getValueType(), ValueTable,
        {Builder.getInt32(0), Builder.getInt32(Slot.Slot)});
    Builder.CreateStore(Builder.CreatePtrToInt(GV, Builder.getInt64Ty()),
                        SlotAddress);
  }

  Value *NullPointer = ConstantPointerNull::get(Builder.getPtrTy());
  Value *RawResult = Builder.CreateCall(
      Execute,
      {Builder.CreatePointerCast(Code, Builder.getPtrTy()),
       Builder.getInt32(Compiled.Code.size() * kInstructionSize),
       FunctionTable
           ? Builder.CreatePointerCast(FunctionTable, Builder.getPtrTy())
           : NullPointer,
       ValueTable ? Builder.CreatePointerCast(ValueTable, Builder.getPtrTy())
                  : NullPointer,
       Builder.getInt32(Compiled.Constants.size()),
       Builder.CreatePointerCast(ArgumentArray, Builder.getPtrTy()),
       Builder.getInt32(F.arg_size()),
       Builder.getInt32(Compiled.FrameSize)});

  if (F.getReturnType()->isVoidTy()) {
    Builder.CreateRetVoid();
  } else {
    Value *ReturnValue =
        F.getReturnType()->isPointerTy()
            ? Builder.CreateIntToPtr(RawResult, F.getReturnType())
            : Builder.CreateTruncOrBitCast(RawResult, F.getReturnType());
    Builder.CreateRet(ReturnValue);
  }
  return true;
}

} // namespace

PreservedAnalyses VmpPass::run(Module &M, ModuleAnalysisManager &) {
  llvm::vllvm::VLLVMConfig &Config = llvm::vllvm::VLLVMConfig::get();
  Config.registerPassLevels("vmp", 1, 3);
  if (!Config.isEnabled("vmp"))
    return PreservedAnalyses::all();
  bool RestoredPreparation = false;
  SmallVector<Function *, 8> Candidates;
  for (Function &F : M) {
    if (F.hasFnAttribute(VmpProcessedAttr))
      continue;
    if (hasVLLVMAttribute(F, "vmp"))
      Candidates.push_back(&F);
  }
  if (Candidates.empty())
    return PreservedAnalyses::all();

  const Triple TT(M.getTargetTriple());
  const bool SupportedTarget = TT.isAArch64() && TT.isLittleEndian() &&
                               M.getDataLayout().getPointerSizeInBits() == 64;
  SmallVector<CompiledFunction, 8> Compiled;
  for (Function *F : Candidates) {
    std::string Reason;
    if (!SupportedTarget)
      Reason = "仅支持 64 位小端 AArch64";
    std::unique_ptr<Module> PreparedModule;
    Function *PreparedFunction = nullptr;
    std::vector<CompiledFunction::HostCallTarget> HostCalls;
    if (Reason.empty()) {
      // 所有分析和字节码生成都在一次性克隆中完成。原函数直到指令表、运行时
      // 和包装入口均已就绪前都保持完全不变。
      PreparedModule = CloneModule(M);
      PreparedFunction = PreparedModule->getFunction(F->getName());
      if (!PreparedFunction) {
        Reason = "无法在临时 Module 中定位候选函数";
      } else {
        SmallVector<Instruction *, 16> Ignorable;
        for (Instruction &I : instructions(*PreparedFunction))
          if (isIgnorableIntrinsic(I))
            Ignorable.push_back(&I);
        for (Instruction *I : Ignorable)
          I->eraseFromParent();
        // 将 select 指令转换成 branch + PHI
        lowerSelects(*PreparedFunction);
        // 将 switch 指令转换成 if else
        lowerSwitches(*PreparedFunction);
        // 内存 intrinsic 改写为 libc 直调（走 HOSTCALL）
        lowerMemIntrinsics(*PreparedFunction, M);
        // GEP 分解与常量表达式物化交替迭代到不动点：物化会暴露新的
        // GEP 指令（如 PHI incoming 的常量 GEP），GEP 分解又会折叠出
        // 新的 ptrtoint 常量表达式（全局基址）。
        for (int Guard = 0; Guard < 8; ++Guard) {
          const bool Changed = materializeConstantExprs(*PreparedFunction) |
                               lowerGEPs(*PreparedFunction);
          if (!Changed)
            break;
        }
        // 判断是否可以进行虚拟化
        Reason = checkEligibility(*PreparedFunction);
        if (Reason.empty() &&
            // 提取目标函数内所有的函数调用
            !lowerHostCalls(*PreparedFunction, M, HostCalls, Reason))
          Reason = Reason.empty() ? "HOSTCALL lowering 失败" : Reason;
        if (Reason.empty()) {
          // HOSTCALL 打包会把 ptrtoint(@全局) 折回 ConstantExpr，
          // 补一轮物化使其成为可取值的指令。
          materializeConstantExprs(*PreparedFunction);
        }
      }
    }
    if (Reason.empty()) {
      CompiledFunction Result;
      VMPCodegenResult Codegen;
      FunctionCompiler Compiler(*PreparedFunction);
      if (Compiler.compile(Codegen, Reason)) {
        Result.Source = F;
        Result.Code = std::move(Codegen.Code);
        Result.Constants = std::move(Codegen.ValueTable);
        Result.AddressSlots = std::move(Codegen.AddressSlots);
        Result.HostCalls = std::move(HostCalls);
        Result.FrameSize = Codegen.FrameSize;
        Compiled.push_back(std::move(Result));
        continue;
      }
    }
    emitMissed(*F, Reason);
    RestoredPreparation |= restorePreparationAttributes(*F);
  }

  if (Compiled.empty())
    return RestoredPreparation ? PreservedAnalyses::none()
                               : PreservedAnalyses::all();

  // 对于已经编译好的函数，统一将调用约定为 fasscc 的函数修改为 C 函数调用约定
  normalizeFastHostCallTargets(Compiled);
  // 链入 VMP 运行时
  std::string RuntimeError;
  if (!linkRuntime(M, RuntimeError)) {
    for (CompiledFunction &Result : Compiled) {
      emitMissed(*Result.Source, RuntimeError);
      RestoredPreparation |= restorePreparationAttributes(*Result.Source);
    }
    return PreservedAnalyses::none();
  }

  unsigned TableId = 0;
  // 修改原函数为调用 VMP 函数
  for (CompiledFunction &Result : Compiled) {
    std::string Error;
    if (!installWrapper(M, Result, TableId++, Error)) {
      // installWrapper 的失败点位于删除原函数体之前；保持事务式回退。
      emitMissed(*Result.Source, Error);
      restorePreparationAttributes(*Result.Source);
    }
  }
  return PreservedAnalyses::none();
}

} // namespace llvm::vllvm
