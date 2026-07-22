#include "interpreter.h"

#include <cstddef>
#include <cstdint>
#include <limits>

namespace vllvm::vm {
// FunctionTable 直接保存宿主目标地址。所有整数/指针 HOSTCALL 共用一个
// AArch64 C ABI trampoline，避免为每个 FunctionType 生成独立 bridge。
extern "C" std::uint64_t __vllvm_vmp_hostcall_bridge(
    std::uint64_t *Registers, std::uint64_t *StackArguments,
    std::uint32_t ArgumentCount, void *FunctionAddress) noexcept;

namespace {

struct ProgramView {
  const std::uint8_t *code = nullptr;
  std::uint32_t instructionCount = 0;
  const std::uint64_t *values = nullptr;
  std::uint32_t constantCount = 0;
  const void *const *functionTable = nullptr;
};

struct VMContext {
  std::uint64_t regs[kRegisterCount]{};
  ProgramView program{};
  std::uint32_t pc = 0;
  std::uint8_t *stack = nullptr;
  std::uint32_t stackSize = 0;
  VmpTrap trap = VmpTrap::None;
};

[[nodiscard]] constexpr std::uint64_t widthMask(unsigned Bits) noexcept {
  return Bits == 64 ? ~0ULL : ((1ULL << Bits) - 1ULL);
}

[[nodiscard]] constexpr unsigned widthBits(std::uint8_t Aux) noexcept {
  switch (static_cast<ValueWidth>(Aux)) {
  case ValueWidth::I1:
    return 1;
  case ValueWidth::I8:
    return 8;
  case ValueWidth::I16:
    return 16;
  case ValueWidth::I32:
    return 32;
  case ValueWidth::I64:
    return 64;
  case ValueWidth::F32:
  case ValueWidth::F64:
  case ValueWidth::PTR:
    return 0;
  }
  return 0;
}

[[nodiscard]] constexpr std::uint64_t truncateTo(std::uint64_t Value,
                                                 unsigned Bits) noexcept {
  return Value & widthMask(Bits);
}

[[nodiscard]] constexpr std::uint64_t signExtendBits(std::uint64_t Value,
                                                     unsigned Bits) noexcept {
  Value = truncateTo(Value, Bits);
  if (Bits == 64 || (Value & (1ULL << (Bits - 1))) == 0)
    return Value;
  return Value | ~widthMask(Bits);
}

[[nodiscard]] std::uint64_t readU64(const std::uint8_t *Data) noexcept {
  std::uint64_t Value = 0;
  for (unsigned I = 0; I != 8; ++I)
    Value |= static_cast<std::uint64_t>(Data[I]) << (I * 8);
  return Value;
}

[[nodiscard]] std::uint64_t readSized(const std::uint8_t *Data,
                                      unsigned Bytes) noexcept {
  std::uint64_t Value = 0;
  for (unsigned I = 0; I != Bytes; ++I)
    Value |= static_cast<std::uint64_t>(Data[I]) << (I * 8);
  return Value;
}

void writeU64(std::uint8_t *Data, std::uint64_t Value,
              unsigned Bytes) noexcept {
  for (unsigned I = 0; I != Bytes; ++I)
    Data[I] = static_cast<std::uint8_t>(Value >> (I * 8));
}

[[nodiscard]] std::uint64_t readReg(const VMContext &Context, Reg R) noexcept {
  return R == Reg::ZR ? 0 : Context.regs[static_cast<unsigned>(R)];
}

void writeReg(VMContext &Context, Reg R, std::uint64_t Value) noexcept {
  if (R != Reg::ZR)
    Context.regs[static_cast<unsigned>(R)] = Value;
}

[[nodiscard]] const std::uint8_t *
constantAddress(const VMContext &Context, std::uint32_t Index) noexcept {
  return reinterpret_cast<const std::uint8_t *>(Context.program.values) +
         static_cast<std::size_t>(Index) * sizeof(std::uint64_t);
}

[[nodiscard]] std::uint8_t *memoryAddress(VMContext &Context, Reg BaseReg,
                                          std::int64_t Offset,
                                          unsigned Width) noexcept {
  if (BaseReg == Reg::SA) {
    if (Offset < 0 ||
        static_cast<std::uint64_t>(Offset) + Width > Context.stackSize) {
      Context.trap = VmpTrap::StackOutOfRange;
      return nullptr;
    }
    return Context.stack + static_cast<std::size_t>(Offset);
  }

  // 非 SA 基址来自通过资格检查的宿主指针参数，M3 保持原程序的裸指针
  // 访问语义；无效宿主指针与原生代码一样可能触发系统访存异常。
  const std::uint64_t Base = readReg(Context, BaseReg);
  const std::uint64_t Address =
      Offset >= 0 ? Base + static_cast<std::uint64_t>(Offset)
                  : Base - static_cast<std::uint64_t>(-Offset);
  return reinterpret_cast<std::uint8_t *>(static_cast<std::uintptr_t>(Address));
}

[[nodiscard]] std::uint64_t readOperand2(const VMContext &Context,
                                         const Instruction &Inst) noexcept {
  return Inst.format == InstFormat::RRI
             ? static_cast<std::uint64_t>(Inst.signedPayload())
             : readReg(Context, Inst.src2);
}

void executeIntegerBinary(VMContext &Context,
                          const Instruction &Inst) noexcept {
  const unsigned Bits = widthBits(Inst.aux);
  const std::uint64_t Mask = widthMask(Bits);
  const std::uint64_t Lhs = readReg(Context, Inst.src1) & Mask;
  const std::uint64_t Rhs = readOperand2(Context, Inst) & Mask;
  std::uint64_t Value = 0;

  switch (Inst.opcode) {
  case Opcode::ADD:
    Value = Lhs + Rhs;
    break;
  case Opcode::SUB:
    Value = Lhs - Rhs;
    break;
  case Opcode::MUL:
    Value = Lhs * Rhs;
    break;
  case Opcode::UDIV:
  case Opcode::UREM:
    if (Rhs == 0) {
      Context.trap = VmpTrap::DivideByZero;
      return;
    }
    Value = Inst.opcode == Opcode::UDIV ? Lhs / Rhs : Lhs % Rhs;
    break;
  case Opcode::SDIV:
  case Opcode::SREM: {
    if (Rhs == 0) {
      Context.trap = VmpTrap::DivideByZero;
      return;
    }
    const std::uint64_t SignedLhsBits = signExtendBits(Lhs, Bits);
    const std::uint64_t SignedRhsBits = signExtendBits(Rhs, Bits);
    const std::int64_t SignedLhs = static_cast<std::int64_t>(SignedLhsBits);
    const std::int64_t SignedRhs = static_cast<std::int64_t>(SignedRhsBits);
    const std::int64_t Minimum =
        Bits == 64 ? std::numeric_limits<std::int64_t>::min()
                   : -(static_cast<std::int64_t>(1) << (Bits - 1));
    if (SignedLhs == Minimum && SignedRhs == -1) {
      Context.trap = VmpTrap::SignedDivideOverflow;
      return;
    }
    Value = static_cast<std::uint64_t>(Inst.opcode == Opcode::SDIV
                                           ? SignedLhs / SignedRhs
                                           : SignedLhs % SignedRhs);
    break;
  }
  case Opcode::AND:
    Value = Lhs & Rhs;
    break;
  case Opcode::OR:
    Value = Lhs | Rhs;
    break;
  case Opcode::XOR:
    Value = Lhs ^ Rhs;
    break;
  case Opcode::SHL:
  case Opcode::LSHR:
  case Opcode::ASHR:
    if (Rhs >= Bits) {
      Context.trap = VmpTrap::ExplicitTrap;
      return;
    }
    if (Inst.opcode == Opcode::SHL)
      Value = Lhs << Rhs;
    else if (Inst.opcode == Opcode::LSHR)
      Value = Lhs >> Rhs;
    else
      Value = static_cast<std::uint64_t>(
          static_cast<std::int64_t>(signExtendBits(Lhs, Bits)) >> Rhs);
    break;
  default:
    Context.trap = VmpTrap::InvalidOpcode;
    return;
  }
  writeReg(Context, Inst.dst, Value & Mask);
}

void executeCompare(VMContext &Context, const Instruction &Inst) noexcept {
  const std::uint64_t Lhs = readReg(Context, Inst.src1);
  const std::uint64_t Rhs = readReg(Context, Inst.src2);
  const std::int64_t SignedLhs = static_cast<std::int64_t>(Lhs);
  const std::int64_t SignedRhs = static_cast<std::int64_t>(Rhs);
  bool Result = false;
  switch (static_cast<IntPredicate>(Inst.aux)) {
  case IntPredicate::EQ:
    Result = Lhs == Rhs;
    break;
  case IntPredicate::NE:
    Result = Lhs != Rhs;
    break;
  case IntPredicate::UGT:
    Result = Lhs > Rhs;
    break;
  case IntPredicate::UGE:
    Result = Lhs >= Rhs;
    break;
  case IntPredicate::ULT:
    Result = Lhs < Rhs;
    break;
  case IntPredicate::ULE:
    Result = Lhs <= Rhs;
    break;
  case IntPredicate::SGT:
    Result = SignedLhs > SignedRhs;
    break;
  case IntPredicate::SGE:
    Result = SignedLhs >= SignedRhs;
    break;
  case IntPredicate::SLT:
    Result = SignedLhs < SignedRhs;
    break;
  case IntPredicate::SLE:
    Result = SignedLhs <= SignedRhs;
    break;
  }
  writeReg(Context, Inst.dst, Result ? 1 : 0);
}

void dispatch(VMContext &Context, const Instruction &Inst) noexcept {
  switch (Inst.opcode) {
  case Opcode::NOP:
    return;
  case Opcode::MOV:
    writeReg(Context, Inst.dst, readReg(Context, Inst.src1));
    return;
  case Opcode::LDC:
    writeReg(Context, Inst.dst,
             readU64(constantAddress(Context, Inst.payload)));
    return;
  case Opcode::ADD:
  case Opcode::SUB:
  case Opcode::MUL:
  case Opcode::UDIV:
  case Opcode::SDIV:
  case Opcode::UREM:
  case Opcode::SREM:
  case Opcode::AND:
  case Opcode::OR:
  case Opcode::XOR:
  case Opcode::SHL:
  case Opcode::LSHR:
  case Opcode::ASHR:
    executeIntegerBinary(Context, Inst);
    return;
  case Opcode::NOT: {
    const unsigned Bits = widthBits(Inst.aux);
    writeReg(Context, Inst.dst,
             (~readReg(Context, Inst.src1)) & widthMask(Bits));
    return;
  }
  case Opcode::ICMP:
    executeCompare(Context, Inst);
    return;
  case Opcode::TRUNC: {
    const unsigned Bits = widthBits(Inst.aux);
    writeReg(Context, Inst.dst, truncateTo(readReg(Context, Inst.src1), Bits));
    return;
  }
  case Opcode::ZEXT: {
    const unsigned Bits = widthBits(Inst.aux);
    writeReg(Context, Inst.dst, truncateTo(readReg(Context, Inst.src1), Bits));
    return;
  }
  case Opcode::SEXT: {
    const unsigned Bits = widthBits(Inst.aux);
    writeReg(Context, Inst.dst,
             signExtendBits(readReg(Context, Inst.src1), Bits));
    return;
  }
  case Opcode::BITCAST:
    writeReg(Context, Inst.dst, readReg(Context, Inst.src1));
    return;
  case Opcode::LOAD8:
  case Opcode::LOAD16:
  case Opcode::LOAD32:
  case Opcode::LOAD64: {
    const unsigned Width = 1U << (static_cast<unsigned>(Inst.opcode) -
                                  static_cast<unsigned>(Opcode::LOAD8));
    std::uint8_t *Address =
        memoryAddress(Context, Inst.src1, Inst.signedPayload(), Width);
    if (Address != nullptr)
      writeReg(Context, Inst.dst, readSized(Address, Width));
    return;
  }
  case Opcode::STORE8:
  case Opcode::STORE16:
  case Opcode::STORE32:
  case Opcode::STORE64: {
    const unsigned Width = 1U << (static_cast<unsigned>(Inst.opcode) -
                                  static_cast<unsigned>(Opcode::STORE8));
    std::uint8_t *Address =
        memoryAddress(Context, Inst.dst, Inst.signedPayload(), Width);
    if (Address != nullptr)
      writeU64(Address, readReg(Context, Inst.src1), Width);
    return;
  }
  case Opcode::BR:
    Context.pc = static_cast<std::uint32_t>(
        static_cast<std::int64_t>(Context.pc) + Inst.signedPayload());
    return;
  case Opcode::BRCOND:
    if (readReg(Context, Inst.src1) != 0)
      Context.pc = static_cast<std::uint32_t>(
          static_cast<std::int64_t>(Context.pc) + Inst.signedPayload());
    return;
  case Opcode::HOSTCALL: {
    const std::uint32_t Index = hostCallFunctionIndex(Inst.payload);
    const std::uint32_t ArgumentCount = hostCallArgumentCount(Inst.payload);
    const std::uint32_t OverflowSize = hostCallOverflowSize(ArgumentCount);
    if (Context.program.functionTable == nullptr ||
        Context.program.functionTable[Index] == nullptr) {
      Context.trap = VmpTrap::InvalidHostCall;
      return;
    }
    if (OverflowSize > Context.stackSize) {
      Context.trap = VmpTrap::StackOutOfRange;
      return;
    }
    std::uint64_t *StackArguments = nullptr;
    if (OverflowSize != 0)
      StackArguments = reinterpret_cast<std::uint64_t *>(
          Context.stack + Context.stackSize - OverflowSize);
    writeReg(Context, Reg::R0,
             __vllvm_vmp_hostcall_bridge(
                 Context.regs, StackArguments, ArgumentCount,
                 const_cast<void *>(Context.program.functionTable[Index])));
    return;
  }
  case Opcode::TRAP:
    Context.trap = VmpTrap::ExplicitTrap;
    return;
  case Opcode::RET:
  case Opcode::INVALID:
  case Opcode::SITOFP:
  case Opcode::UITOFP:
  case Opcode::FPTOSI:
  case Opcode::FPTOUI:
  case Opcode::FPEXT:
  case Opcode::FPTRUNC:
  case Opcode::FADD32:
  case Opcode::FSUB32:
  case Opcode::FMUL32:
  case Opcode::FDIV32:
  case Opcode::FNEG32:
  case Opcode::FCMP32:
  case Opcode::FADD64:
  case Opcode::FSUB64:
  case Opcode::FMUL64:
  case Opcode::FDIV64:
  case Opcode::FNEG64:
  case Opcode::FCMP64:
  case Opcode::VMCALL:
    Context.trap = VmpTrap::InvalidOpcode;
    return;
  }
}

[[nodiscard]] VmpTrap
executeChecked(const std::uint64_t *Code, std::uint32_t CodeSize,
               const void *const *FunctionTable,
               const std::uint64_t *ValueTable, std::uint32_t ValueCount,
               std::uint64_t *Arguments, std::uint32_t ArgumentCount,
               std::uint8_t *Stack, std::uint32_t StackSize,
               std::uint64_t &Result) noexcept {
  static_assert(sizeof(std::uintptr_t) == sizeof(std::uint64_t),
                "M3 解释器只支持 64 位宿主指针");

  if (Code == nullptr || CodeSize == 0 || (CodeSize % kInstructionSize) != 0)
    return VmpTrap::InvalidFormat;
  if (ArgumentCount > kMaxArgumentCount ||
      (ArgumentCount != 0 && Arguments == nullptr) ||
      (ValueCount != 0 && ValueTable == nullptr))
    return VmpTrap::InvalidFormat;
  if (StackSize > kMaxFrameSize || (StackSize != 0 && Stack == nullptr))
    return VmpTrap::StackOverflow;
  if (Stack != nullptr &&
      (reinterpret_cast<std::uintptr_t>(Stack) % kFrameAlignment) != 0)
    return VmpTrap::StackOutOfRange;

  VMContext Context;
  Context.program.code = reinterpret_cast<const std::uint8_t *>(Code);
  Context.program.instructionCount = CodeSize / kInstructionSize;
  Context.program.values = ValueTable;
  Context.program.constantCount = ValueCount;
  Context.program.functionTable = FunctionTable;
  for (std::uint32_t I = 0; I != ArgumentCount; ++I)
    Context.regs[I] = Arguments[I];
  Context.regs[static_cast<unsigned>(Reg::SA)] =
      static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(Stack));
  Context.stack = Stack;
  Context.stackSize = StackSize;
  Context.pc = 0;

  while (Context.trap == VmpTrap::None) {
    if (Context.pc >= Context.program.instructionCount) {
      Context.trap = VmpTrap::PcOutOfRange;
      break;
    }
    const std::uint32_t CurrentPc = Context.pc;
    const Instruction Inst = Instruction::decode(
        readU64(Context.program.code +
                static_cast<std::size_t>(CurrentPc) * kInstructionSize));
    Context.trap =
        validateM3Instruction(Inst, CurrentPc, Context.program.instructionCount,
                              Context.program.constantCount);
    if (Context.trap != VmpTrap::None)
      break;

    // REL32 以“已递增的下一条 PC”为基准，分支 Handler 只需再加 Payload。
    Context.pc = CurrentPc + 1;
    if (Inst.opcode == Opcode::RET) {
      Result = readReg(Context, Reg::R0);
      return VmpTrap::None;
    }
    dispatch(Context, Inst);
  }
  return Context.trap;
}

} // namespace

extern "C" std::uint64_t
__vllvm_vmp_execute(const std::uint64_t *Code, std::uint32_t CodeSize,
                    const void *const *FunctionTable,
                    const std::uint64_t *ValueTable, std::uint32_t ValueCount,
                    std::uint64_t *Arguments, std::uint32_t ArgumentCount,
                    std::uint8_t *Stack, std::uint32_t StackSize) noexcept {
  std::uint64_t Result = 0;
  if (executeChecked(Code, CodeSize, FunctionTable, ValueTable, ValueCount,
                     Arguments, ArgumentCount, Stack, StackSize,
                     Result) != VmpTrap::None)
    __builtin_trap();
  return Result;
}

#if defined(VLLVM_VMP_AARCH64_RUNTIME) || defined(__aarch64__)

// VM 调用约定把前六个参数放在 R0-R5，其余参数放在 StackArguments。这里把
// 第七、八个参数补到 AArch64 x6/x7，并把第九个及以后复制到宿主 SP。
// naked 函数只包含 basic asm，符号名仍由 LLVM 按最终 Mach-O/ELF/COFF
// 目标生成，因此同一份 runtime bitcode 可用于三个 AArch64 平台。
extern "C" __attribute__((naked, noinline)) std::uint64_t
__vllvm_vmp_hostcall_bridge(std::uint64_t *, std::uint64_t *, std::uint32_t,
                            void *) noexcept {
  __asm__ volatile(
      "stp x29, x30, [sp, #-48]!\n"
      "mov x29, sp\n"
      "stp x19, x20, [sp, #16]\n"
      "stp x21, x22, [sp, #32]\n"
      "mov x20, x0\n"
      "mov x21, x1\n"
      "mov w22, w2\n"
      "mov x16, x3\n"
      "cmp w22, #8\n"
      "b.ls 2f\n"
      "sub w9, w22, #8\n"
      "uxtw x9, w9\n"
      "lsl x9, x9, #3\n"
      "add x9, x9, #15\n"
      "and x9, x9, #-16\n"
      "sub sp, sp, x9\n"
      "add x10, x21, #16\n"
      "mov x11, sp\n"
      "sub w12, w22, #8\n"
      "1:\n"
      "ldr x13, [x10], #8\n"
      "str x13, [x11], #8\n"
      "subs w12, w12, #1\n"
      "b.ne 1b\n"
      "2:\n"
      "ldp x0, x1, [x20]\n"
      "ldp x2, x3, [x20, #16]\n"
      "ldp x4, x5, [x20, #32]\n"
      "cmp w22, #6\n"
      "b.ls 3f\n"
      "ldr x6, [x21]\n"
      "cmp w22, #7\n"
      "b.ls 3f\n"
      "ldr x7, [x21, #8]\n"
      "3:\n"
      "blr x16\n"
      "mov sp, x29\n"
      "ldp x19, x20, [sp, #16]\n"
      "ldp x21, x22, [sp, #32]\n"
      "ldp x29, x30, [sp], #48\n"
      "ret\n");
}

#elif defined(VLLVM_VMP_TESTING)

// 非 AArch64 主机只会运行解释器单测，不会进入正式 VMP Pass。保留一个
// 纯 C++ 的八参数测试实现，使 ISA Handler 测试不依赖测试机架构。
extern "C" std::uint64_t __vllvm_vmp_hostcall_bridge(
    std::uint64_t *Registers, std::uint64_t *StackArguments,
    std::uint32_t ArgumentCount, void *FunctionAddress) noexcept {
  using TestTarget = std::uint64_t (*)(std::uint64_t, std::uint64_t,
                                       std::uint64_t, std::uint64_t,
                                       std::uint64_t, std::uint64_t,
                                       std::uint64_t, std::uint64_t);
  if (ArgumentCount != 8)
    return 0;
  return reinterpret_cast<TestTarget>(FunctionAddress)(
      Registers[0], Registers[1], Registers[2], Registers[3], Registers[4],
      Registers[5], StackArguments[0], StackArguments[1]);
}

#endif

#if defined(VLLVM_VMP_TESTING)
extern "C" std::uint32_t __vllvm_vmp_execute_checked(
    const std::uint64_t *Code, std::uint32_t CodeSize,
    const void *const *FunctionTable, const std::uint64_t *ValueTable,
    std::uint32_t ValueCount, std::uint64_t *Arguments,
    std::uint32_t ArgumentCount, std::uint8_t *Stack, std::uint32_t StackSize,
    std::uint64_t *Result) noexcept {
  if (Result == nullptr)
    return static_cast<std::uint32_t>(VmpTrap::InvalidFormat);
  return static_cast<std::uint32_t>(
      executeChecked(Code, CodeSize, FunctionTable, ValueTable, ValueCount,
                     Arguments, ArgumentCount, Stack, StackSize, *Result));
}
#endif

} // namespace vllvm::vm
