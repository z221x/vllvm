#include "llvm/Transforms/VLLVM/VmpFunctionCompiler.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/Instructions.h"
#include "llvm/Support/Alignment.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/Transforms/VLLVM/VmpCommon.h"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <string>
#include <utility>
#include <vector>

using namespace llvm;
using namespace ::vllvm::vm;
using VmInstruction = ::vllvm::vm::Instruction;

namespace {

constexpr StringLiteral HostCallPrefix = "__vllvm_vmp_hostcall.";
constexpr StringLiteral OutgoingAllocaName = "vmp.hostcall.stack.args";

struct BranchFixup {
  std::uint32_t InstructionIndex = 0;
  const BasicBlock *Target = nullptr;
};

class FunctionCompilerImpl {
public:
  explicit FunctionCompilerImpl(Function &F)
      : F(F), DL(llvm::vllvm::kVMPDataLayout) {}

  bool compile(std::vector<std::uint64_t> &EncodedCode,
               std::vector<std::uint64_t> &ValueTable,
               std::uint32_t &OutputFrameSize, std::string &Error) {
    if (!layoutFrame(Error) || !selectFunction(Error) ||
        !resolveBranches(Error) || !validate(Error))
      return false;

    EncodedCode.reserve(Code.size());
    for (const VmInstruction &Inst : Code)
      EncodedCode.push_back(Inst.encode());
    ValueTable = Constants;
    OutputFrameSize = FrameSize;
    return true;
  }

private:
  Function &F;
  const DataLayout DL;
  DenseMap<const Value *, std::uint32_t> ValueSlots;
  DenseMap<const AllocaInst *, std::uint32_t> StackObjects;
  DenseMap<std::uint64_t, std::uint32_t> ConstantIndices;
  DenseMap<const BasicBlock *, std::uint32_t> BlockPcs;
  SmallVector<BranchFixup, 32> Branches;
  SmallVector<VmInstruction, 128> Code;
  std::vector<std::uint64_t> Constants;
  const AllocaInst *OutgoingAlloca = nullptr;
  std::uint32_t PhiTemporaryBase = 0;
  std::uint32_t PhiTemporaryCount = 0;
  std::uint32_t FrameSize = 0;

  static std::uint8_t valueWidth(Type *Ty) {
    if (Ty->isPointerTy())
      return static_cast<std::uint8_t>(ValueWidth::PTR);
    const unsigned Bits = cast<IntegerType>(Ty)->getBitWidth();
    switch (Bits) {
    case 1:
      return static_cast<std::uint8_t>(ValueWidth::I1);
    case 8:
      return static_cast<std::uint8_t>(ValueWidth::I8);
    case 16:
      return static_cast<std::uint8_t>(ValueWidth::I16);
    case 32:
      return static_cast<std::uint8_t>(ValueWidth::I32);
    default:
      return static_cast<std::uint8_t>(ValueWidth::I64);
    }
  }

  static unsigned byteWidth(Type *Ty) {
    if (Ty->isPointerTy())
      return 8;
    const unsigned Bits = cast<IntegerType>(Ty)->getBitWidth();
    return std::max(1U, Bits / 8U);
  }

  static Opcode loadOpcode(unsigned Bytes) {
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

  static Opcode storeOpcode(unsigned Bytes) {
    switch (Bytes) {
    case 1:
      return Opcode::STORE8;
    case 2:
      return Opcode::STORE16;
    case 4:
      return Opcode::STORE32;
    default:
      return Opcode::STORE64;
    }
  }

  static bool hasResultSlot(const llvm::Instruction &I) {
    return !I.getType()->isVoidTy() && !isa<AllocaInst, GetElementPtrInst>(I);
  }

  static bool isOutgoingAlloca(const AllocaInst &AI) {
    return AI.getMetadata("vllvm.vmp.hostcall.outgoing") != nullptr ||
           AI.getName() == OutgoingAllocaName;
  }

  bool allocate(std::uint64_t Size, Align Alignment, std::uint64_t &Offset,
                std::uint32_t &Assigned, std::string &Error) const {
    Offset = alignTo(Offset, Alignment.value());
    if (Size > kMaxFrameSize || Offset > kMaxFrameSize - Size) {
      Error = "VMP Direct CodeGen 栈帧超过 1 MiB";
      return false;
    }
    Assigned = static_cast<std::uint32_t>(Offset);
    Offset += Size;
    return true;
  }

  bool allocaSize(const AllocaInst &AI, std::uint64_t &Size,
                  std::string &Error) const {
    auto *Count = dyn_cast<ConstantInt>(AI.getArraySize());
    if (!Count) {
      Error = "VMP Direct CodeGen 不支持动态 alloca";
      return false;
    }
    TypeSize ElementSize = DL.getTypeAllocSize(AI.getAllocatedType());
    if (ElementSize.isScalable()) {
      Error = "VMP Direct CodeGen 不支持 scalable alloca";
      return false;
    }
    const std::uint64_t ElementBytes = ElementSize.getFixedValue();
    const std::uint64_t CountValue = Count->getZExtValue();
    if (CountValue != 0 &&
        ElementBytes > std::numeric_limits<std::uint64_t>::max() / CountValue) {
      Error = "VMP alloca 尺寸溢出";
      return false;
    }
    Size = ElementBytes * CountValue;
    return true;
  }

  bool layoutFrame(std::string &Error) {
    std::uint64_t Offset = 0;

    // HOSTCALL 溢出参数区必须位于帧尾；解释器根据 stackSize 反向定位它。
    for (llvm::Instruction &I : instructions(F)) {
      auto *AI = dyn_cast<AllocaInst>(&I);
      if (!AI)
        continue;
      if (isOutgoingAlloca(*AI)) {
        if (OutgoingAlloca) {
          Error = "VMP Direct CodeGen 只允许一个 HOSTCALL 溢出参数区";
          return false;
        }
        OutgoingAlloca = AI;
        continue;
      }
      std::uint64_t Size = 0;
      std::uint32_t Assigned = 0;
      if (!allocaSize(*AI, Size, Error) ||
          !allocate(Size, AI->getAlign(), Offset, Assigned, Error))
        return false;
      StackObjects[AI] = Assigned;
    }

    for (Argument &Arg : F.args()) {
      std::uint32_t Assigned = 0;
      if (!allocate(8, Align(8), Offset, Assigned, Error))
        return false;
      ValueSlots[&Arg] = Assigned;
    }
    for (llvm::Instruction &I : instructions(F)) {
      if (!hasResultSlot(I))
        continue;
      std::uint32_t Assigned = 0;
      if (!allocate(8, Align(8), Offset, Assigned, Error))
        return false;
      ValueSlots[&I] = Assigned;
    }

    for (BasicBlock &BB : F) {
      std::uint32_t Count = 0;
      for (PHINode &Phi : BB.phis())
        (void)Phi, ++Count;
      PhiTemporaryCount = std::max(PhiTemporaryCount, Count);
    }
    if (PhiTemporaryCount != 0) {
      if (!allocate(static_cast<std::uint64_t>(PhiTemporaryCount) * 8, Align(8),
                    Offset, PhiTemporaryBase, Error))
        return false;
    }

    Offset = alignTo(Offset, kFrameAlignment);
    if (OutgoingAlloca) {
      std::uint64_t Size = 0;
      std::uint32_t Assigned = 0;
      if (!allocaSize(*OutgoingAlloca, Size, Error) ||
          !allocate(Size, Align(kFrameAlignment), Offset, Assigned, Error))
        return false;
      StackObjects[OutgoingAlloca] = Assigned;
    }
    Offset = alignTo(Offset, kFrameAlignment);
    if (Offset > kMaxFrameSize) {
      Error = "VMP Direct CodeGen 栈帧超过 1 MiB";
      return false;
    }
    FrameSize = static_cast<std::uint32_t>(Offset);

    if (OutgoingAlloca) {
      std::uint64_t Size = 0;
      if (!allocaSize(*OutgoingAlloca, Size, Error))
        return false;
      const std::uint64_t Expected =
          static_cast<std::uint64_t>(FrameSize) - Size;
      if (StackObjects.lookup(OutgoingAlloca) != Expected) {
        Error = "HOSTCALL 溢出参数区没有落在 VM 栈帧尾部";
        return false;
      }
    }
    return true;
  }

  void emit(const VmInstruction &Inst) { Code.push_back(Inst); }

  std::uint32_t addConstant(std::uint64_t Value) {
    auto [It, Inserted] = ConstantIndices.try_emplace(Value, Constants.size());
    if (Inserted)
      Constants.push_back(Value);
    return It->second;
  }

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

  void emitLoadStack(Reg Dst, std::uint32_t Offset) {
    emit({.opcode = Opcode::LOAD64,
          .dst = Dst,
          .src1 = Reg::SA,
          .payload = Offset,
          .format = InstFormat::MEM_SRC});
  }

  void emitStoreStack(std::uint32_t Offset, Reg Source) {
    emit({.opcode = Opcode::STORE64,
          .dst = Reg::SA,
          .src1 = Source,
          .payload = Offset,
          .format = InstFormat::MEM_DST});
  }

  bool resolveStackAddress(const Value *V, std::int64_t &Offset) const {
    if (const auto *AI = dyn_cast<AllocaInst>(V)) {
      auto It = StackObjects.find(AI);
      if (It == StackObjects.end())
        return false;
      Offset = It->second;
      return true;
    }
    const auto *GEP = dyn_cast<GetElementPtrInst>(V);
    if (!GEP)
      return false;
    std::int64_t BaseOffset = 0;
    if (!resolveStackAddress(GEP->getPointerOperand(), BaseOffset))
      return false;
    APInt Relative(64, 0, true);
    if (!GEP->accumulateConstantOffset(DL, Relative))
      return false;
    const std::int64_t Delta = Relative.getSExtValue();
    if (llvm::AddOverflow(BaseOffset, Delta, Offset))
      return false;
    return Offset >= 0 && Offset <= kMaxFrameSize;
  }

  bool emitValue(const Value *V, Reg Dst, std::string &Error) {
    if (const auto *CI = dyn_cast<ConstantInt>(V)) {
      emitConstant(Dst, CI->getValue().getZExtValue());
      return true;
    }
    if (isa<ConstantPointerNull, UndefValue, PoisonValue>(V)) {
      emitConstant(Dst, 0);
      return true;
    }

    std::int64_t StackOffset = 0;
    if (resolveStackAddress(V, StackOffset)) {
      if (StackOffset == 0) {
        emit({.opcode = Opcode::MOV,
              .dst = Dst,
              .src1 = Reg::SA,
              .format = InstFormat::RRR});
      } else {
        emit({.opcode = Opcode::ADD,
              .aux = static_cast<std::uint8_t>(ValueWidth::I64),
              .dst = Dst,
              .src1 = Reg::SA,
              .payload = static_cast<std::uint32_t>(StackOffset),
              .format = InstFormat::RRI});
      }
      return true;
    }

    auto It = ValueSlots.find(V);
    if (It == ValueSlots.end()) {
      Error =
          (Twine("VMP Direct CodeGen 无法取得 SSA 值：") + V->getName()).str();
      return false;
    }
    emitLoadStack(Dst, It->second);
    return true;
  }

  bool storeResult(const llvm::Instruction &I, Reg Source, std::string &Error) {
    auto It = ValueSlots.find(&I);
    if (It == ValueSlots.end()) {
      Error = (Twine("VMP Direct CodeGen 缺少结果栈槽：") + I.getOpcodeName())
                  .str();
      return false;
    }
    emitStoreStack(It->second, Source);
    return true;
  }

  bool directMemoryAddress(const Value *Pointer, Reg &Base,
                           std::uint32_t &Payload, std::string &Error) {
    std::int64_t Offset = 0;
    if (resolveStackAddress(Pointer, Offset)) {
      if (!isInt<32>(Offset)) {
        Error = "VMP 栈访问偏移超出 32 位 Payload";
        return false;
      }
      Base = Reg::SA;
      Payload = static_cast<std::uint32_t>(Offset);
      return true;
    }
    if (!emitValue(Pointer, Reg::R10, Error))
      return false;
    Base = Reg::R10;
    Payload = 0;
    return true;
  }

  static bool binaryOpcode(unsigned IROpcode, Opcode &VmOpcode) {
    switch (IROpcode) {
    case llvm::Instruction::Add:
      VmOpcode = Opcode::ADD;
      return true;
    case llvm::Instruction::Sub:
      VmOpcode = Opcode::SUB;
      return true;
    case llvm::Instruction::Mul:
      VmOpcode = Opcode::MUL;
      return true;
    case llvm::Instruction::UDiv:
      VmOpcode = Opcode::UDIV;
      return true;
    case llvm::Instruction::SDiv:
      VmOpcode = Opcode::SDIV;
      return true;
    case llvm::Instruction::URem:
      VmOpcode = Opcode::UREM;
      return true;
    case llvm::Instruction::SRem:
      VmOpcode = Opcode::SREM;
      return true;
    case llvm::Instruction::And:
      VmOpcode = Opcode::AND;
      return true;
    case llvm::Instruction::Or:
      VmOpcode = Opcode::OR;
      return true;
    case llvm::Instruction::Xor:
      VmOpcode = Opcode::XOR;
      return true;
    case llvm::Instruction::Shl:
      VmOpcode = Opcode::SHL;
      return true;
    case llvm::Instruction::LShr:
      VmOpcode = Opcode::LSHR;
      return true;
    case llvm::Instruction::AShr:
      VmOpcode = Opcode::ASHR;
      return true;
    default:
      return false;
    }
  }

  static bool comparePredicate(CmpInst::Predicate Predicate,
                               IntPredicate &VmPredicate) {
    switch (Predicate) {
    case CmpInst::ICMP_EQ:
      VmPredicate = IntPredicate::EQ;
      return true;
    case CmpInst::ICMP_NE:
      VmPredicate = IntPredicate::NE;
      return true;
    case CmpInst::ICMP_UGT:
      VmPredicate = IntPredicate::UGT;
      return true;
    case CmpInst::ICMP_UGE:
      VmPredicate = IntPredicate::UGE;
      return true;
    case CmpInst::ICMP_ULT:
      VmPredicate = IntPredicate::ULT;
      return true;
    case CmpInst::ICMP_ULE:
      VmPredicate = IntPredicate::ULE;
      return true;
    case CmpInst::ICMP_SGT:
      VmPredicate = IntPredicate::SGT;
      return true;
    case CmpInst::ICMP_SGE:
      VmPredicate = IntPredicate::SGE;
      return true;
    case CmpInst::ICMP_SLT:
      VmPredicate = IntPredicate::SLT;
      return true;
    case CmpInst::ICMP_SLE:
      VmPredicate = IntPredicate::SLE;
      return true;
    default:
      return false;
    }
  }

  bool selectBinary(const BinaryOperator &BO, std::string &Error) {
    Opcode VmOpcode = Opcode::INVALID;
    if (!binaryOpcode(BO.getOpcode(), VmOpcode)) {
      Error = "VMP Direct CodeGen 不支持该二元运算";
      return false;
    }
    if (!emitValue(BO.getOperand(0), Reg::R10, Error) ||
        !emitValue(BO.getOperand(1), Reg::R11, Error))
      return false;
    emit({.opcode = VmOpcode,
          .aux = valueWidth(BO.getType()),
          .dst = Reg::R12,
          .src1 = Reg::R10,
          .src2 = Reg::R11,
          .format = InstFormat::RRR});
    return storeResult(BO, Reg::R12, Error);
  }

  bool selectCompare(const ICmpInst &Cmp, std::string &Error) {
    IntPredicate Predicate = IntPredicate::EQ;
    if (!comparePredicate(Cmp.getPredicate(), Predicate)) {
      Error = "VMP Direct CodeGen 不支持该整数比较谓词";
      return false;
    }
    if (!emitValue(Cmp.getOperand(0), Reg::R10, Error) ||
        !emitValue(Cmp.getOperand(1), Reg::R11, Error))
      return false;

    Type *OperandType = Cmp.getOperand(0)->getType();
    const std::uint8_t Width = valueWidth(OperandType);
    if (!OperandType->isPointerTy() &&
        Width != static_cast<std::uint8_t>(ValueWidth::I64)) {
      const bool Signed = Cmp.isSigned();
      const Opcode Normalize = Signed ? Opcode::SEXT : Opcode::ZEXT;
      emit({.opcode = Normalize,
            .aux = Width,
            .dst = Reg::R10,
            .src1 = Reg::R10,
            .format = InstFormat::RRR});
      emit({.opcode = Normalize,
            .aux = Width,
            .dst = Reg::R11,
            .src1 = Reg::R11,
            .format = InstFormat::RRR});
    }
    emit({.opcode = Opcode::ICMP,
          .aux = static_cast<std::uint8_t>(Predicate),
          .dst = Reg::R12,
          .src1 = Reg::R10,
          .src2 = Reg::R11,
          .format = InstFormat::RRR});
    return storeResult(Cmp, Reg::R12, Error);
  }

  bool selectLoad(const LoadInst &Load, std::string &Error) {
    Reg Base = Reg::ZR;
    std::uint32_t Payload = 0;
    if (!directMemoryAddress(Load.getPointerOperand(), Base, Payload, Error))
      return false;
    emit({.opcode = loadOpcode(byteWidth(Load.getType())),
          .dst = Reg::R12,
          .src1 = Base,
          .payload = Payload,
          .format = InstFormat::MEM_SRC});
    return storeResult(Load, Reg::R12, Error);
  }

  bool selectStore(const StoreInst &Store, std::string &Error) {
    Reg Base = Reg::ZR;
    std::uint32_t Payload = 0;
    if (!directMemoryAddress(Store.getPointerOperand(), Base, Payload, Error) ||
        !emitValue(Store.getValueOperand(), Reg::R11, Error))
      return false;
    emit({.opcode = storeOpcode(byteWidth(Store.getValueOperand()->getType())),
          .dst = Base,
          .src1 = Reg::R11,
          .payload = Payload,
          .format = InstFormat::MEM_DST});
    return true;
  }

  bool selectCast(const CastInst &Cast, std::string &Error) {
    if (!emitValue(Cast.getOperand(0), Reg::R10, Error))
      return false;
    Opcode VmOpcode = Opcode::INVALID;
    std::uint8_t Aux = 0;
    if (isa<TruncInst>(Cast)) {
      VmOpcode = Opcode::TRUNC;
      Aux = valueWidth(Cast.getDestTy());
    } else if (isa<ZExtInst>(Cast)) {
      VmOpcode = Opcode::ZEXT;
      Aux = valueWidth(Cast.getSrcTy());
    } else if (isa<SExtInst>(Cast)) {
      VmOpcode = Opcode::SEXT;
      Aux = valueWidth(Cast.getSrcTy());
    } else if (isa<BitCastInst, PtrToIntInst, IntToPtrInst>(Cast)) {
      VmOpcode = Opcode::BITCAST;
    } else {
      Error = "VMP Direct CodeGen 不支持该转换";
      return false;
    }
    emit({.opcode = VmOpcode,
          .aux = Aux,
          .dst = Reg::R12,
          .src1 = Reg::R10,
          .format = InstFormat::RRR});
    return storeResult(Cast, Reg::R12, Error);
  }

  bool parseHostCall(const CallInst &Call, std::uint32_t &Index,
                     std::uint32_t &ArgumentCount, std::string &Error) const {
    const Function *Callee = Call.getCalledFunction();
    if (!Callee) {
      Error = "VMP Direct CodeGen 只支持直接 HOSTCALL";
      return false;
    }
    StringRef Suffix = Callee->getName();
    if (!Suffix.consume_front(HostCallPrefix)) {
      Error = "VMP Direct CodeGen 遇到未 lowering 的函数调用";
      return false;
    }
    auto [IndexText, CountText] = Suffix.split('.');
    if (IndexText.empty() || CountText.empty() ||
        IndexText.getAsInteger(10, Index) ||
        CountText.getAsInteger(10, ArgumentCount) ||
        Index > kHostCallIndexMask ||
        ArgumentCount > kMaxHostCallArgumentCount) {
      Error = "VMP HOSTCALL 伪符号格式无效";
      return false;
    }
    const std::uint32_t RegisterCount =
        std::min(ArgumentCount, kMaxArgumentCount);
    if (Call.arg_size() != RegisterCount) {
      Error = "VMP HOSTCALL 寄存器参数数量与伪符号不一致";
      return false;
    }
    return true;
  }

  bool selectCall(const CallInst &Call, std::string &Error) {
    std::uint32_t Index = 0;
    std::uint32_t ArgumentCount = 0;
    if (!parseHostCall(Call, Index, ArgumentCount, Error))
      return false;
    for (unsigned I = 0; I != Call.arg_size(); ++I) {
      const Reg ArgumentRegister = static_cast<Reg>(I);
      if (!emitValue(Call.getArgOperand(I), ArgumentRegister, Error))
        return false;
    }
    emit({.opcode = Opcode::HOSTCALL,
          .payload = packHostCallPayload(Index, ArgumentCount),
          .format = InstFormat::CALL});
    if (!Call.getType()->isVoidTy())
      return storeResult(Call, Reg::R0, Error);
    return true;
  }

  bool selectInstruction(const llvm::Instruction &I, std::string &Error) {
    if (isa<PHINode, AllocaInst, GetElementPtrInst>(I))
      return true;
    if (const auto *BO = dyn_cast<BinaryOperator>(&I))
      return selectBinary(*BO, Error);
    if (const auto *Cmp = dyn_cast<ICmpInst>(&I))
      return selectCompare(*Cmp, Error);
    if (const auto *Load = dyn_cast<LoadInst>(&I))
      return selectLoad(*Load, Error);
    if (const auto *Store = dyn_cast<StoreInst>(&I))
      return selectStore(*Store, Error);
    if (const auto *Cast = dyn_cast<CastInst>(&I))
      return selectCast(*Cast, Error);
    if (const auto *Call = dyn_cast<CallInst>(&I))
      return selectCall(*Call, Error);
    Error = (Twine("VMP Direct CodeGen 不支持 IR 指令：") + I.getOpcodeName())
                .str();
    return false;
  }

  bool emitPhiCopies(const BasicBlock &Predecessor, const BasicBlock &Target,
                     std::string &Error) {
    unsigned Index = 0;
    for (const PHINode &Phi : Target.phis()) {
      const Value *Incoming = Phi.getIncomingValueForBlock(&Predecessor);
      if (!Incoming) {
        Error = "VMP PHI 缺少当前控制流边的 incoming value";
        return false;
      }
      if (!emitValue(Incoming, Reg::R10, Error))
        return false;
      emitStoreStack(PhiTemporaryBase + Index * 8, Reg::R10);
      ++Index;
    }
    Index = 0;
    for (const PHINode &Phi : Target.phis()) {
      auto Slot = ValueSlots.find(&Phi);
      if (Slot == ValueSlots.end()) {
        Error = "VMP PHI 缺少结果栈槽";
        return false;
      }
      emitLoadStack(Reg::R10, PhiTemporaryBase + Index * 8);
      emitStoreStack(Slot->second, Reg::R10);
      ++Index;
    }
    return true;
  }

  void emitBranch(const BasicBlock *Target) {
    Branches.push_back({static_cast<std::uint32_t>(Code.size()), Target});
    emit({.opcode = Opcode::BR, .format = InstFormat::REL32});
  }

  bool selectTerminator(const BasicBlock &BB, std::string &Error) {
    const llvm::Instruction *Terminator = BB.getTerminator();
    if (const auto *Return = dyn_cast<ReturnInst>(Terminator)) {
      if (const Value *Value = Return->getReturnValue())
        if (!emitValue(Value, Reg::R0, Error))
          return false;
      emit({.opcode = Opcode::RET, .format = InstFormat::NONE});
      return true;
    }
    if (isa<UnreachableInst>(Terminator)) {
      emit({.opcode = Opcode::TRAP, .format = InstFormat::NONE});
      return true;
    }
    const auto *Branch = dyn_cast<BranchInst>(Terminator);
    if (!Branch) {
      Error = "VMP Direct CodeGen 只支持 branch/ret/unreachable 终结指令";
      return false;
    }
    if (Branch->isUnconditional()) {
      const BasicBlock *Target = Branch->getSuccessor(0);
      if (!emitPhiCopies(BB, *Target, Error))
        return false;
      emitBranch(Target);
      return true;
    }

    if (!emitValue(Branch->getCondition(), Reg::R10, Error))
      return false;
    const std::uint32_t ConditionalIndex = Code.size();
    emit({.opcode = Opcode::BRCOND,
          .src1 = Reg::R10,
          .format = InstFormat::REL32});

    const BasicBlock *FalseTarget = Branch->getSuccessor(1);
    if (!emitPhiCopies(BB, *FalseTarget, Error))
      return false;
    emitBranch(FalseTarget);

    const std::uint32_t TrueEdgePc = Code.size();
    const std::int64_t TrueDelta =
        static_cast<std::int64_t>(TrueEdgePc) - ConditionalIndex - 1;
    Code[ConditionalIndex].payload = static_cast<std::uint32_t>(TrueDelta);
    const BasicBlock *TrueTarget = Branch->getSuccessor(0);
    if (!emitPhiCopies(BB, *TrueTarget, Error))
      return false;
    emitBranch(TrueTarget);
    return true;
  }

  bool selectFunction(std::string &Error) {
    unsigned ArgumentIndex = 0;
    BlockPcs[&F.getEntryBlock()] = 0;
    for (Argument &Arg : F.args()) {
      emitStoreStack(ValueSlots.lookup(&Arg),
                     static_cast<Reg>(ArgumentIndex++));
    }

    for (BasicBlock &BB : F) {
      if (&BB != &F.getEntryBlock())
        BlockPcs[&BB] = Code.size();
      for (llvm::Instruction &I : BB) {
        if (I.isTerminator())
          break;
        if (!selectInstruction(I, Error))
          return false;
      }
      if (!selectTerminator(BB, Error))
        return false;
    }
    return !Code.empty();
  }

  bool resolveBranches(std::string &Error) {
    for (const BranchFixup &Fixup : Branches) {
      auto Target = BlockPcs.find(Fixup.Target);
      if (Target == BlockPcs.end()) {
        Error = "VMP 分支目标没有指令地址";
        return false;
      }
      const std::int64_t Delta =
          static_cast<std::int64_t>(Target->second) -
          static_cast<std::int64_t>(Fixup.InstructionIndex) - 1;
      if (!isInt<32>(Delta)) {
        Error = "VMP REL32 分支超出范围";
        return false;
      }
      Code[Fixup.InstructionIndex].payload = static_cast<std::uint32_t>(Delta);
    }
    return true;
  }

  bool validate(std::string &Error) const {
    if (Code.size() > std::numeric_limits<std::uint32_t>::max() ||
        Constants.size() > std::numeric_limits<std::uint32_t>::max()) {
      Error = "VMP 指令表或常量表超过 32 位流格式上限";
      return false;
    }
    for (std::uint32_t Pc = 0; Pc != Code.size(); ++Pc) {
      const VmpTrap Trap =
          validateM3Instruction(Code[Pc], Pc, Code.size(), Constants.size());
      if (Trap != VmpTrap::None) {
        Error = "VMP Direct CodeGen 结果验证失败，pc=" + utostr(Pc) +
                " trap=" + utostr(static_cast<std::uint32_t>(Trap));
        return false;
      }
    }
    return true;
  }
};

} // namespace

llvm::vllvm::FunctionCompiler::FunctionCompiler(Function &F) : F(F) {}

bool llvm::vllvm::FunctionCompiler::compile(VMPCodegenResult &Output,
                                            std::string &Error) {
  VMPCodegenResult Result;
  FunctionCompilerImpl Compiler(F);
  if (!Compiler.compile(Result.Code, Result.ValueTable, Result.FrameSize,
                        Error))
    return false;
  for (std::uint32_t Pc = 0; Pc != Result.Code.size(); ++Pc) {
    const VmpTrap Trap =
        validateM3Instruction(VmInstruction::decode(Result.Code[Pc]), Pc,
                              Result.Code.size(), Result.ValueTable.size());
    if (Trap != VmpTrap::None) {
      Error = "VMP 编码结果未通过 ISA 验证，pc=" + utostr(Pc) +
              " trap=" + utostr(static_cast<std::uint32_t>(Trap));
      return false;
    }
  }
  Output = std::move(Result);
  return true;
}
