#include "VmpPass.h"

#include "VLLVMAttribute.h"
#include "VmpCommon.h"
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
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/IR/LegacyPassManager.h"
#include "llvm/IR/Module.h"
#include "llvm/Linker/Linker.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Object/ObjectFile.h"
#include "llvm/Support/Alignment.h"
#include "llvm/Support/CodeGen.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/Support/MemoryBufferRef.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Target/TargetMachine.h"
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
using namespace llvm::object;
using ::vllvm::vm::InstFormat;
using ::vllvm::vm::IntPredicate;
using ::vllvm::vm::kFrameAlignment;
using ::vllvm::vm::kInstructionSize;
using ::vllvm::vm::kMaxArgumentCount;
using ::vllvm::vm::kMaxFrameSize;
using ::vllvm::vm::Opcode;
using ::vllvm::vm::Reg;
using ::vllvm::vm::ValueWidth;
using ::vllvm::vm::VmpTrap;
using VmInstruction = ::vllvm::vm::Instruction;

extern "C" void LLVMInitializeVMPTargetInfo();
extern "C" void LLVMInitializeVMPTarget();
extern "C" void LLVMInitializeVMPTargetMC();
extern "C" void LLVMInitializeVMPAsmPrinter();

namespace llvm::vllvm {
namespace {

constexpr StringLiteral VmpProcessedAttr = "vllvm.vmp.processed";
constexpr StringLiteral VmpInjectedNoInlineAttr = "vllvm.vmp.injected.noinline";
constexpr StringLiteral VmpHadAlwaysInlineAttr = "vllvm.vmp.had.alwaysinline";
constexpr StringLiteral RuntimeName = "__vllvm_vmp_execute";
constexpr StringLiteral RuntimeHostBridgeName =
    "__vllvm_vmp_hostcall_bridge";
constexpr StringLiteral RuntimeAttr = "vllvm.vmp.runtime";

struct CompiledFunction {
  Function *Source = nullptr;
  std::vector<std::uint64_t> Code;
  std::vector<std::uint64_t> Constants;
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

[[nodiscard]] std::string
checkFastCallNormalization(const Function &Target) {
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
  if (std::string Reason = checkFastCallNormalization(*Target);
      !Reason.empty())
    return Reason;
  if (Target->isVarArg())
    return "M3 HOSTCALL 不支持可变参数目标";
  if (Call.arg_size() != Target->arg_size() ||
      Call.getFunctionType() != Target->getFunctionType())
    return "HOSTCALL 调用点类型与直接目标函数类型不一致";
  if (Call.arg_size() > ::vllvm::vm::kMaxHostCallArgumentCount)
    return "HOSTCALL 参数数量超过 Payload 的 8 位 argc 上限";
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
      if (!AI->isStaticAlloca() || AI->isArrayAllocation() ||
          !isScalarVmType(AI->getAllocatedType()) ||
          AI->getAlign().value() > kFrameAlignment)
        return "只支持对齐不超过 16 字节的标量静态 alloca";
      continue;
    }
    if (auto *LI = dyn_cast<LoadInst>(&I)) {
      if (LI->isVolatile() || LI->isAtomic() || !isScalarVmType(LI->getType()))
        return "load 必须是非 volatile、非 atomic 的 M3 标量访问";
      continue;
    }
    if (auto *SI = dyn_cast<StoreInst>(&I)) {
      if (SI->isVolatile() || SI->isAtomic() ||
          !isScalarVmType(SI->getValueOperand()->getType()))
        return "store 必须是非 volatile、非 atomic 的 M3 标量访问";
      continue;
    }
    if (isa<ICmpInst, PHINode, BranchInst, SwitchInst, SelectInst, ReturnInst,
            UnreachableInst>(&I))
      continue;
    if (auto *CI = dyn_cast<CastInst>(&I)) {
      if (isa<TruncInst, ZExtInst, SExtInst, BitCastInst>(CI) &&
          isScalarVmType(CI->getSrcTy()) && isScalarVmType(CI->getDestTy()))
        continue;
      return "只支持 trunc/zext/sext/等宽 bitcast";
    }
    if (isa<GetElementPtrInst>(&I))
      return "M3 暂不支持 GEP";
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
                         "__vllvm_vmp_hostcall." + utostr(Targets.size()),
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
      RegisterArguments.push_back(
          packHostCallScalar(Builder, *Call, I));

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
          StoreBytes = ArgumentType->isPointerTy()
                           ? 8
                           : std::max(1U, cast<IntegerType>(ArgumentType)
                                               ->getBitWidth() /
                                           8);
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
          Packed = Builder.CreateTrunc(
              Packed, Builder.getIntNTy(StoreBytes * 8));
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

struct MachineBranchFixup {
  std::size_t InstructionIndex = 0;
  std::uint32_t TargetSlot = 0;
};

struct MachineHostCall {
  std::uint32_t FunctionIndex = 0;
  std::uint32_t ArgumentCount = 0;
};

// M2 使用 LLVM 的 SelectionDAG、PHI 消除和寄存器分配生成临时小端 ELF。
// 当前 Target 以裁剪的 BPF Machine IR 为过渡层；此处只读取已分配后的固定
// 64 位机器指令，并立即翻译到冻结的 VMP ISA，BPF 编码不会进入最终程序。
class TargetBytecodeCompiler {
public:
  explicit TargetBytecodeCompiler(
      Function &F, ArrayRef<CompiledFunction::HostCallTarget> HostCalls)
      : F(F), HostCalls(HostCalls) {}

  bool compile(CompiledFunction &Output, std::string &Error) {
    SmallVector<char, 0> ObjectBytes;
    if (!emitObject(ObjectBytes, Error))
      return false;

    Expected<std::unique_ptr<ObjectFile>> Object = ObjectFile::createObjectFile(
        MemoryBufferRef(StringRef(ObjectBytes.data(), ObjectBytes.size()),
                        "vmp-temporary.o"));
    if (!Object) {
      Error = "无法解析 VMP Target 临时 ELF：" + toString(Object.takeError());
      return false;
    }

    StringRef MachineCode;
    StringRef MetaContents;
    bool HasConstantSection = false;
    bool HasMetaSection = false;
    std::string SectionNames;
    for (SectionRef Section : (*Object)->sections()) {
      Expected<StringRef> Name = Section.getName();
      if (!Name) {
        Error = toString(Name.takeError());
        return false;
      }
      if (!Name->empty())
        SectionNames += (Twine("[") + *Name + "]").str();
      if (*Name == ".vmp.const")
        HasConstantSection = true;
      if (*Name == ".vmp.meta")
        HasMetaSection = true;
      if (*Name != ".vmp.code") {
        if (*Name == ".vmp.meta") {
          Expected<StringRef> Contents = Section.getContents();
          if (!Contents) {
            Error = toString(Contents.takeError());
            return false;
          }
          MetaContents = *Contents;
        }
        continue;
      }
      Expected<StringRef> Contents = Section.getContents();
      if (!Contents) {
        Error = toString(Contents.takeError());
        return false;
      }
      MachineCode = *Contents;
    }
    for (SectionRef Section : (*Object)->sections()) {
      Expected<StringRef> Name = Section.getName();
      if (!Name) {
        Error = toString(Name.takeError());
        return false;
      }
      if (*Name != ".rel.vmp.code" && *Name != ".rela.vmp.code")
        continue;
      for (RelocationRef Relocation : Section.relocations()) {
        symbol_iterator Symbol = Relocation.getSymbol();
        if (Symbol == (*Object)->symbol_end()) {
          Error = "VMP CALL relocation 缺少目标符号";
          return false;
        }
        Expected<StringRef> SymbolName = Symbol->getName();
        if (!SymbolName) {
          Error = toString(SymbolName.takeError());
          return false;
        }
        auto Target = llvm::find_if(
            HostCalls, [&](const CompiledFunction::HostCallTarget &Candidate) {
              return Candidate.PseudoSymbol == *SymbolName;
            });
        if (Target == HostCalls.end()) {
          Error = "VMP 代码节包含未知 relocation: " + SymbolName->str();
          return false;
        }
        const std::uint64_t Offset = Relocation.getOffset();
        if (Offset >= MachineCode.size()) {
          Error = "VMP CALL relocation 超出代码节";
          return false;
        }
        const std::uint32_t Slot = Offset / kInstructionSize;
        if (!MachineHostCalls
                 .try_emplace(Slot,
                              MachineHostCall{static_cast<std::uint32_t>(
                                                  Target - HostCalls.begin()),
                                              Target->ArgumentCount})
                 .second) {
          Error = "同一 VMP 机器指令存在多个 CALL relocation";
          return false;
        }
      }
    }
    if (MachineCode.empty() || !HasConstantSection || !HasMetaSection) {
      Error = "VMP Target 对象节不完整(code=" + utostr(MachineCode.size()) +
              ", const=" + (HasConstantSection ? "1" : "0") +
              ", meta=" + (HasMetaSection ? "1" : "0") +
              ", sections=" + SectionNames + ")";
      return false;
    }
    if (MetaContents.empty()) {
      Error = ".vmp.meta 为空";
      return false;
    }
    if (!translateMachineCode(MachineCode, Error) ||
        !validateGeneratedCode(Error) || !finalizeOutput(Output, Error))
      return false;
    Output.Source = &F;
    return true;
  }

private:
  Function &F;
  ArrayRef<CompiledFunction::HostCallTarget> HostCalls;
  SmallVector<VmInstruction, 128> Code;
  SmallVector<MachineBranchFixup, 32> Fixups;
  DenseMap<std::uint32_t, MachineHostCall> MachineHostCalls;
  DenseMap<std::uint64_t, std::uint32_t> ConstantIndices;
  std::vector<std::uint64_t> Constants;
  std::uint32_t FrameSize = 0;

  static void initializeTarget() {
    static const bool Initialized = [] {
      LLVMInitializeVMPTargetInfo();
      LLVMInitializeVMPTarget();
      LLVMInitializeVMPTargetMC();
      LLVMInitializeVMPAsmPrinter();
      return true;
    }();
    (void)Initialized;
  }

  bool emitObject(SmallVectorImpl<char> &Bytes, std::string &Error) {
    initializeTarget();
    Triple TargetTriple("bpfel-unknown-none");
    const Target *TheTarget =
        TargetRegistry::lookupTarget("vmp", TargetTriple, Error);
    if (!TheTarget)
      return false;

    TargetOptions Options;
    std::unique_ptr<TargetMachine> TM(TheTarget->createTargetMachine(
        TargetTriple, "v4", "", Options, Reloc::PIC_, CodeModel::Small,
        CodeGenOptLevel::Default));
    if (!TM) {
      Error = "无法创建实验性 VMP TargetMachine";
      return false;
    }

    Module &M = *F.getParent();
    M.setTargetTriple(TargetTriple);
    M.setDataLayout(TM->createDataLayout());
    F.setSection(".vmp.code");
    for (Function &Current : M) {
      Current.removeFnAttr("target-cpu");
      Current.removeFnAttr("target-features");
      Current.removeFnAttr("tune-cpu");
      Current.removeFnAttr(Attribute::StackProtect);
      Current.removeFnAttr(Attribute::StackProtectReq);
      Current.removeFnAttr(Attribute::StackProtectStrong);
      if (&Current != &F && !Current.isDeclaration())
        Current.deleteBody();
    }

    legacy::PassManager PM;
    raw_svector_ostream Stream(Bytes);
    if (TM->addPassesToEmitFile(PM, Stream, nullptr,
                                CodeGenFileType::ObjectFile, false)) {
      Error = "实验性 VMP Target 不支持临时 ELF 输出";
      return false;
    }
    PM.run(M);
    return true;
  }

  static std::uint16_t read16(const std::uint8_t *Data) {
    return static_cast<std::uint16_t>(Data[0]) |
           (static_cast<std::uint16_t>(Data[1]) << 8);
  }

  static std::uint32_t read32(const std::uint8_t *Data) {
    std::uint32_t Value = 0;
    for (unsigned I = 0; I != 4; ++I)
      Value |= static_cast<std::uint32_t>(Data[I]) << (I * 8);
    return Value;
  }

  static unsigned memoryWidth(std::uint8_t MachineOpcode) {
    switch ((MachineOpcode >> 3) & 3U) {
    case 0:
      return 4;
    case 1:
      return 2;
    case 2:
      return 1;
    default:
      return 8;
    }
  }

  static std::uint8_t widthForBytes(unsigned Bytes) {
    switch (Bytes) {
    case 1:
      return static_cast<std::uint8_t>(ValueWidth::I8);
    case 2:
      return static_cast<std::uint8_t>(ValueWidth::I16);
    case 4:
      return static_cast<std::uint8_t>(ValueWidth::I32);
    default:
      return static_cast<std::uint8_t>(ValueWidth::I64);
    }
  }

  static Opcode loadForBytes(unsigned Bytes) {
    switch (Bytes) {
    case 1:
      return Opcode::LOAD8;
    case 2:
      return Opcode::LOAD16;
    case 4:
      return Opcode::LOAD32;
    default:
      return Opcode::LOAD64;
    }
  }

  static Opcode storeForBytes(unsigned Bytes) {
    return static_cast<Opcode>(static_cast<unsigned>(loadForBytes(Bytes)) + 4);
  }

  bool mapRegister(unsigned MachineReg, Reg &Mapped, std::string &Error) const {
    if (MachineReg <= 9) {
      Mapped = static_cast<Reg>(MachineReg);
      return true;
    }
    Error = "VMP Target 产生了非通用保留寄存器 R" + utostr(MachineReg);
    return false;
  }

  std::uint32_t addConstant(std::uint64_t Value) {
    auto It = ConstantIndices.find(Value);
    if (It != ConstantIndices.end())
      return It->second;
    const std::uint32_t Index = static_cast<std::uint32_t>(Constants.size());
    ConstantIndices[Value] = Index;
    Constants.push_back(Value);
    return Index;
  }

  void emit(const VmInstruction &Inst) { Code.push_back(Inst); }

  void emitConstant(Reg Dst, std::uint64_t Value) {
    if (Value == 0) {
      emit({.opcode = Opcode::MOV,
            .dst = Dst,
            .src1 = Reg::ZR,
            .format = InstFormat::RRR});
      return;
    }
    emit({.opcode = Opcode::LDC,
          .dst = Dst,
          .payload = addConstant(Value),
          .format = InstFormat::CONST_POOL});
  }

  bool scanFrame(StringRef Bytes, std::string &Error) {
    const auto *Data = reinterpret_cast<const std::uint8_t *>(Bytes.data());
    const std::uint32_t Slots = Bytes.size() / kInstructionSize;
    std::uint64_t Depth = 0;
    for (std::uint32_t Slot = 0; Slot < Slots; ++Slot) {
      const std::uint8_t Op = Data[Slot * 8];
      if (Op == 0x18) {
        ++Slot;
        continue;
      }
      const unsigned Class = Op & 7U;
      if (Class != 1 && Class != 2 && Class != 3)
        continue;
      const std::uint8_t Regs = Data[Slot * 8 + 1];
      const unsigned Base = Class == 1 ? Regs >> 4 : Regs & 0x0fU;
      if (Base != 10)
        continue;
      const std::int16_t Offset =
          static_cast<std::int16_t>(read16(Data + Slot * 8 + 2));
      if (Offset >= 0) {
        Error = "VMP Target 产生了非负 FrameIndex 偏移";
        return false;
      }
      Depth =
          std::max<std::uint64_t>(Depth, -static_cast<std::int64_t>(Offset));
    }
    Depth = alignTo(Depth, kFrameAlignment);
    if (Depth > kMaxFrameSize) {
      Error = "寄存器分配后的 VM 栈帧超过 1 MiB";
      return false;
    }
    FrameSize = static_cast<std::uint32_t>(Depth);
    return true;
  }

  bool translateMemory(const std::uint8_t *Inst, std::uint8_t MachineOpcode,
                       std::string &Error) {
    const unsigned Class = MachineOpcode & 7U;
    const unsigned Mode = MachineOpcode >> 5;
    const unsigned Width = memoryWidth(MachineOpcode);
    const unsigned DstMachine = Inst[1] & 0x0fU;
    const unsigned SrcMachine = Inst[1] >> 4;
    const std::int16_t MachineOffset =
        static_cast<std::int16_t>(read16(Inst + 2));
    Reg Base = Reg::ZR;
    const unsigned BaseMachine = DstMachine;
    std::int64_t Offset = MachineOffset;
    if (BaseMachine == 10) {
      Base = Reg::SA;
      Offset += FrameSize;
      if (Offset < 0 || Offset + Width > FrameSize) {
        Error = "FrameIndex 消除后的栈访问越界";
        return false;
      }
    } else if (!mapRegister(BaseMachine, Base, Error)) {
      return false;
    }

    if (Class == 1) {
      // LDX 的 base 位于 src nibble，而 dst 位于 dst nibble。
      if (SrcMachine == 10) {
        Base = Reg::SA;
        Offset = static_cast<std::int64_t>(MachineOffset) + FrameSize;
        if (Offset < 0 || Offset + Width > FrameSize) {
          Error = "FrameIndex 消除后的栈 load 越界";
          return false;
        }
      } else if (!mapRegister(SrcMachine, Base, Error)) {
        return false;
      }
      Reg Dst = Reg::ZR;
      if (!mapRegister(DstMachine, Dst, Error))
        return false;
      emit({.opcode = loadForBytes(Width),
            .dst = Dst,
            .src1 = Base,
            .payload = static_cast<std::uint32_t>(Offset),
            .format = InstFormat::MEM_SRC});
      if (Mode == 4) {
        emit({.opcode = Opcode::SEXT,
              .aux = widthForBytes(Width),
              .dst = Dst,
              .src1 = Dst,
              .format = InstFormat::RRR});
      } else if (Mode != 3) {
        Error = "VMP Target 产生了不支持的 load 模式";
        return false;
      }
      return true;
    }

    if (Mode != 3) {
      Error = "VMP Target 产生了不支持的 store 模式";
      return false;
    }
    Reg Value = Reg::R12;
    if (Class == 3) {
      if (!mapRegister(SrcMachine, Value, Error))
        return false;
    } else {
      const std::int64_t Immediate =
          static_cast<std::int32_t>(read32(Inst + 4));
      emitConstant(Value, static_cast<std::uint64_t>(Immediate));
    }
    emit({.opcode = storeForBytes(Width),
          .dst = Base,
          .src1 = Value,
          .payload = static_cast<std::uint32_t>(Offset),
          .format = InstFormat::MEM_DST});
    return true;
  }

  static bool predicateForJump(unsigned JumpOp, IntPredicate &Predicate) {
    switch (JumpOp) {
    case 1:
      Predicate = IntPredicate::EQ;
      return true;
    case 2:
      Predicate = IntPredicate::UGT;
      return true;
    case 3:
      Predicate = IntPredicate::UGE;
      return true;
    case 5:
      Predicate = IntPredicate::NE;
      return true;
    case 6:
      Predicate = IntPredicate::SGT;
      return true;
    case 7:
      Predicate = IntPredicate::SGE;
      return true;
    case 10:
      Predicate = IntPredicate::ULT;
      return true;
    case 11:
      Predicate = IntPredicate::ULE;
      return true;
    case 12:
      Predicate = IntPredicate::SLT;
      return true;
    case 13:
      Predicate = IntPredicate::SLE;
      return true;
    default:
      return false;
    }
  }

  bool translateJump(const std::uint8_t *Inst, std::uint8_t MachineOpcode,
                     std::uint32_t Slot, std::string &Error) {
    const unsigned JumpOp = MachineOpcode >> 4;
    const unsigned Class = MachineOpcode & 7U;
    const bool RegisterRhs = (MachineOpcode & 8U) != 0;
    const std::int16_t Relative = static_cast<std::int16_t>(read16(Inst + 2));
    const std::uint32_t TargetSlot = static_cast<std::uint32_t>(
        static_cast<std::int64_t>(Slot) + 1 + Relative);
    if (JumpOp == 0) {
      Fixups.push_back({Code.size(), TargetSlot});
      emit({.opcode = Opcode::BR, .format = InstFormat::REL32});
      return true;
    }
    if (JumpOp == 9) {
      emit({.opcode = Opcode::RET});
      return true;
    }
    if (JumpOp == 8) {
      auto Call = MachineHostCalls.find(Slot);
      if (Call == MachineHostCalls.end()) {
        Error = "VMP 机器 CALL 缺少目标 relocation，slot=" + utostr(Slot) +
                "，已记录=";
        for (const auto &Entry : MachineHostCalls)
          Error += utostr(Entry.first) + ",";
        return false;
      }
      emit({.opcode = Opcode::HOSTCALL,
            .payload = ::vllvm::vm::packHostCallPayload(
                Call->second.FunctionIndex, Call->second.ArgumentCount),
            .format = InstFormat::CALL});
      return true;
    }

    Reg Lhs = Reg::ZR;
    Reg Rhs = Reg::R13;
    if (!mapRegister(Inst[1] & 0x0fU, Lhs, Error))
      return false;
    if (RegisterRhs) {
      if (!mapRegister(Inst[1] >> 4, Rhs, Error))
        return false;
    } else {
      const std::int64_t Immediate =
          static_cast<std::int32_t>(read32(Inst + 4));
      emitConstant(Rhs, static_cast<std::uint64_t>(Immediate));
    }

    const bool Is32 = Class == 6;
    const bool IsSigned =
        JumpOp == 6 || JumpOp == 7 || JumpOp == 12 || JumpOp == 13;
    if (Is32) {
      emit({.opcode = IsSigned ? Opcode::SEXT : Opcode::TRUNC,
            .aux = static_cast<std::uint8_t>(ValueWidth::I32),
            .dst = Reg::R12,
            .src1 = Lhs,
            .format = InstFormat::RRR});
      emit({.opcode = IsSigned ? Opcode::SEXT : Opcode::TRUNC,
            .aux = static_cast<std::uint8_t>(ValueWidth::I32),
            .dst = Reg::R13,
            .src1 = Rhs,
            .format = InstFormat::RRR});
      Lhs = Reg::R12;
      Rhs = Reg::R13;
    }

    if (JumpOp == 4) {
      emit({.opcode = Opcode::AND,
            .aux = static_cast<std::uint8_t>(Is32 ? ValueWidth::I32
                                                  : ValueWidth::I64),
            .dst = Reg::R12,
            .src1 = Lhs,
            .src2 = Rhs,
            .format = InstFormat::RRR});
    } else {
      IntPredicate Predicate = IntPredicate::EQ;
      if (!predicateForJump(JumpOp, Predicate)) {
        Error = "VMP Target 产生了未知条件分支";
        return false;
      }
      emit({.opcode = Opcode::ICMP,
            .aux = static_cast<std::uint8_t>(Predicate),
            .dst = Reg::R12,
            .src1 = Lhs,
            .src2 = Rhs,
            .format = InstFormat::RRR});
    }
    Fixups.push_back({Code.size(), TargetSlot});
    emit({.opcode = Opcode::BRCOND,
          .src1 = Reg::R12,
          .format = InstFormat::REL32});
    return true;
  }

  bool translateAlu(const std::uint8_t *Inst, std::uint8_t MachineOpcode,
                    std::string &Error) {
    const unsigned Class = MachineOpcode & 7U;
    const bool Is32 = Class == 4;
    const bool RegisterRhs = (MachineOpcode & 8U) != 0;
    const unsigned AluOp = MachineOpcode >> 4;
    const std::int16_t Variant = static_cast<std::int16_t>(read16(Inst + 2));
    Reg Dst = Reg::ZR;
    if (!mapRegister(Inst[1] & 0x0fU, Dst, Error))
      return false;

    if (AluOp == 11) {
      if (RegisterRhs) {
        Reg Source = Reg::ZR;
        if (!mapRegister(Inst[1] >> 4, Source, Error))
          return false;
        if (Variant == 8 || Variant == 16 || Variant == 32) {
          emit({.opcode = Opcode::SEXT,
                .aux = widthForBytes(Variant / 8),
                .dst = Dst,
                .src1 = Source,
                .format = InstFormat::RRR});
        } else {
          emit({.opcode = Is32 ? Opcode::TRUNC : Opcode::MOV,
                .aux = static_cast<std::uint8_t>(Is32 ? ValueWidth::I32
                                                      : ValueWidth::I1),
                .dst = Dst,
                .src1 = Source,
                .format = InstFormat::RRR});
          if (!Is32)
            Code.back().aux = 0;
        }
      } else {
        const std::int64_t Immediate =
            static_cast<std::int32_t>(read32(Inst + 4));
        const std::uint64_t Bits = Is32 ? static_cast<std::uint32_t>(Immediate)
                                        : static_cast<std::uint64_t>(Immediate);
        emitConstant(Dst, Bits);
      }
      return true;
    }

    if (AluOp == 8) {
      emit({.opcode = Opcode::SUB,
            .aux = static_cast<std::uint8_t>(Is32 ? ValueWidth::I32
                                                  : ValueWidth::I64),
            .dst = Dst,
            .src1 = Reg::ZR,
            .src2 = Dst,
            .format = InstFormat::RRR});
      return true;
    }

    Opcode Op = Opcode::INVALID;
    switch (AluOp) {
    case 0:
      Op = Opcode::ADD;
      break;
    case 1:
      Op = Opcode::SUB;
      break;
    case 2:
      Op = Opcode::MUL;
      break;
    case 3:
      Op = Variant == 1 ? Opcode::SDIV : Opcode::UDIV;
      break;
    case 4:
      Op = Opcode::OR;
      break;
    case 5:
      Op = Opcode::AND;
      break;
    case 6:
      Op = Opcode::SHL;
      break;
    case 7:
      Op = Opcode::LSHR;
      break;
    case 9:
      Op = Variant == 1 ? Opcode::SREM : Opcode::UREM;
      break;
    case 10:
      Op = Opcode::XOR;
      break;
    case 12:
      Op = Opcode::ASHR;
      break;
    default:
      Error = "VMP Target 产生了不支持的 ALU Opcode";
      return false;
    }

    VmInstruction Output{
        .opcode = Op,
        .aux =
            static_cast<std::uint8_t>(Is32 ? ValueWidth::I32 : ValueWidth::I64),
        .dst = Dst,
        .src1 = Dst,
        .format = RegisterRhs ? InstFormat::RRR : InstFormat::RRI};
    if (RegisterRhs) {
      if (!mapRegister(Inst[1] >> 4, Output.src2, Error))
        return false;
    } else {
      Output.src2 = Reg::ZR;
      Output.payload = read32(Inst + 4);
    }
    emit(Output);
    return true;
  }

  bool translateMachineCode(StringRef Bytes, std::string &Error) {
    if ((Bytes.size() % kInstructionSize) != 0) {
      Error = ".vmp.code 不是 64 位指令序列";
      return false;
    }
    if (!scanFrame(Bytes, Error))
      return false;

    const auto *Data = reinterpret_cast<const std::uint8_t *>(Bytes.data());
    const std::uint32_t Slots = Bytes.size() / kInstructionSize;
    SmallVector<std::uint32_t, 128> SlotToPc(
        Slots + 1, std::numeric_limits<std::uint32_t>::max());
    for (std::uint32_t Slot = 0; Slot < Slots;) {
      SlotToPc[Slot] = Code.size();
      const std::uint8_t *Inst = Data + Slot * kInstructionSize;
      const std::uint8_t MachineOpcode = Inst[0];
      if (MachineOpcode == 0x18) {
        if (Slot + 1 >= Slots) {
          Error = "截断的 64 位 Target 常量";
          return false;
        }
        Reg Dst = Reg::ZR;
        if (!mapRegister(Inst[1] & 0x0fU, Dst, Error))
          return false;
        const std::uint64_t Value =
            read32(Inst + 4) |
            (static_cast<std::uint64_t>(read32(Data + (Slot + 1) * 8 + 4))
             << 32);
        emitConstant(Dst, Value);
        SlotToPc[Slot + 1] = SlotToPc[Slot];
        Slot += 2;
        continue;
      }

      const unsigned Class = MachineOpcode & 7U;
      bool Ok = false;
      if (Class == 1 || Class == 2 || Class == 3)
        Ok = translateMemory(Inst, MachineOpcode, Error);
      else if (Class == 4 || Class == 7)
        Ok = translateAlu(Inst, MachineOpcode, Error);
      else if (Class == 5 || Class == 6)
        Ok = translateJump(Inst, MachineOpcode, Slot, Error);
      else
        Error = "VMP Target 产生了不支持的机器指令类";
      if (!Ok)
        return false;
      ++Slot;
    }
    SlotToPc[Slots] = Code.size();

    for (const MachineBranchFixup &Fixup : Fixups) {
      if (Fixup.TargetSlot >= Slots ||
          SlotToPc[Fixup.TargetSlot] ==
              std::numeric_limits<std::uint32_t>::max()) {
        Error = "Target 分支指向无效的机器指令槽";
        return false;
      }
      const std::int64_t Delta =
          static_cast<std::int64_t>(SlotToPc[Fixup.TargetSlot]) -
          static_cast<std::int64_t>(Fixup.InstructionIndex + 1);
      if (!isInt<32>(Delta)) {
        Error = "翻译后的 VMP REL32 分支超出范围";
        return false;
      }
      Code[Fixup.InstructionIndex].payload = static_cast<std::uint32_t>(Delta);
    }
    return true;
  }

  bool validateGeneratedCode(std::string &Error) const {
    for (std::uint32_t Pc = 0; Pc != Code.size(); ++Pc) {
      const VmpTrap Trap = ::vllvm::vm::validateM3Instruction(
          Code[Pc], Pc, Code.size(), Constants.size());
      if (Trap != VmpTrap::None) {
        Error = "Target 翻译结果未通过 ISA 验证，pc=" + utostr(Pc) +
                " trap=" + utostr(static_cast<std::uint32_t>(Trap));
        return false;
      }
    }
    return true;
  }

  bool finalizeOutput(CompiledFunction &Output, std::string &Error) const {
    const std::uint64_t CodeSize =
        static_cast<std::uint64_t>(Code.size()) * kInstructionSize;
    if (CodeSize > std::numeric_limits<std::uint32_t>::max() ||
        Constants.size() > std::numeric_limits<std::uint32_t>::max()) {
      Error = "VMP 指令表或常量表超过 32 位 ABI 尺寸上限";
      return false;
    }
    Output.Code.clear();
    Output.Code.reserve(Code.size());
    for (const VmInstruction &Inst : Code)
      Output.Code.push_back(Inst.encode());
    Output.Constants = Constants;
    Output.HostCalls.assign(HostCalls.begin(), HostCalls.end());
    Output.FrameSize = FrameSize;
    return true;
  }
};

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
  for (StringRef Attr : {"vllvm.fop", "vllvm.fla", "vllvm.icall", "vllvm.ibr",
                         "vllvm.lvars", "vllvm.bcf", "vllvm.vmp"})
    F.removeFnAttr(Attr);
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
    ValueTable = new GlobalVariable(
        M, ValueData->getType(), true, GlobalValue::PrivateLinkage, ValueData,
        "__vllvm_vmp_values." + F.getName() + "." + utostr(TableId));
    ValueTable->setUnnamedAddr(GlobalValue::UnnamedAddr::Global);
    ValueTable->setAlignment(Align(8));
  }

  GlobalVariable *FunctionTable = nullptr;
  if (!Compiled.HostCalls.empty()) {
    SmallVector<Constant *, 16> Entries;
    Entries.reserve(Compiled.HostCalls.size());
    for (const CompiledFunction::HostCallTarget &HostCall :
         Compiled.HostCalls)
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

  F.deleteBody();
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

  const unsigned StackBytes = std::max<std::uint32_t>(1, Compiled.FrameSize);
  ArrayType *StackTy = ArrayType::get(Builder.getInt8Ty(), StackBytes);
  AllocaInst *Stack = Builder.CreateAlloca(StackTy);
  Stack->setAlignment(Align(kFrameAlignment));

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
       Builder.CreatePointerCast(Stack, Builder.getPtrTy()),
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
      Reason = "M3 仅支持 64 位小端 AArch64 目标";
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
        // 在临时 Module 内显式形成 branch+PHI，Target 不需要依赖宿主优化
        // 级别决定 select/switch 的最终形状。
        lowerSelects(*PreparedFunction);
        lowerSwitches(*PreparedFunction);
        Reason = checkEligibility(*PreparedFunction);
        if (Reason.empty() &&
            !lowerHostCalls(*PreparedFunction, M, HostCalls, Reason))
          Reason = Reason.empty() ? "HOSTCALL lowering 失败" : Reason;
      }
    }
    if (Reason.empty()) {
      CompiledFunction Result;
      TargetBytecodeCompiler Compiler(*PreparedFunction, HostCalls);
      if (Compiler.compile(Result, Reason)) {
        Result.Source = F;
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

  // 优化器可能把仅在当前 Module 内使用的普通 C 函数改成 fastcc。资格检查已
  // 保证这些目标没有地址逃逸且所有用途都是直接 fastcc 调用；统一改回 C 后，
  // FunctionTable 才能安全保存其真实地址供通用 AArch64 trampoline 调用。
  normalizeFastHostCallTargets(Compiled);

  std::string RuntimeError;
  if (!linkRuntime(M, RuntimeError)) {
    for (CompiledFunction &Result : Compiled) {
      emitMissed(*Result.Source, RuntimeError);
      RestoredPreparation |= restorePreparationAttributes(*Result.Source);
    }
    return PreservedAnalyses::none();
  }

  unsigned TableId = 0;
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
