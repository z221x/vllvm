#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>

namespace vllvm::vm {

/*
 * VM 执行模型
 * -----------
 * - 所有寄存器都是无类型的 64 位值，由整数或浮点 Opcode 决定如何解释其位模式。
 * - SA 指向当前 VM 栈帧；V1 没有 VM 调用栈，RET 结束顶层解释执行。
 * - 小于 64 位的整数通过 TRUNC/ZEXT/SEXT 指令进行合法化。
 * - 比较指令向 Dst 写入 0 或 1，BRCOND 读取该值。这样 LLVM i1 值可以独立
 *   存活，不需要经过全局标志寄存器。
 *
 * 解密后的 64 位指令布局
 *
 *  63          52 51      48 47  44 43  40 39  36 35             4 3   1 0
 * +--------------+----------+------+------+------+----------------+-----+-+
 * | opcode (12)  | aux (4)  | dst  | src1 | src2 | payload (32)   | fmt |E|
 * +--------------+----------+------+------+------+----------------+-----+-+
 *
 * Aux 的含义由 Opcode 决定。ICMP/FCMP 将其作为比较谓词，其他 Opcode 可以
 * 将其用于数据宽度、转换类型或 Handler 变体。
 *
 * E 位在解密前始终可读。Instruction::encode() 只负责打包字段和设置 E 位；
 * 当 E 为 1 时，由字节码加密层负责加密 bit 63..1。
 */

inline constexpr std::uint8_t kRegisterCount = 16;
inline constexpr std::uint8_t kInstructionSize = 8;

// R0-R13 有意设计为无类型寄存器，F32/F64 Opcode 会重新解释其中的位模式。
enum class Reg : std::uint8_t {
  R0 = 0,
  R1,
  R2,
  R3,
  R4,
  R5,
  R6,
  R7,
  R8,
  R9,
  R10,
  R11,
  R12,
  R13,
  SA, // 当前 VM 栈帧地址。
  ZR, // 读取时恒为零，写入的数据会被丢弃。
};

// 逻辑操作码。字节码生成器可以为每个受保护函数将这些值随机映射为 12 位
// Token，再由解释器的解码表将 Token 映射回逻辑 Opcode。
enum class Opcode : std::uint16_t {
  INVALID = 0x000,
  NOP = 0x001,
  MOV = 0x002,
  LDC = 0x003, // Dst = ValueTable[Payload]。

  ADD = 0x010,
  SUB = 0x011,
  MUL = 0x012,
  UDIV = 0x013,
  SDIV = 0x014,
  UREM = 0x015,
  SREM = 0x016,
  AND = 0x017,
  OR = 0x018,
  XOR = 0x019,
  NOT = 0x01A,
  SHL = 0x01B,
  LSHR = 0x01C,
  ASHR = 0x01D,
  ICMP = 0x01E, // Aux 是 IntPredicate，Dst 接收 0 或 1。

  TRUNC = 0x020,
  ZEXT = 0x021,
  SEXT = 0x022,
  BITCAST = 0x023,
  SITOFP = 0x024,
  UITOFP = 0x025,
  FPTOSI = 0x026,
  FPTOUI = 0x027,
  FPEXT = 0x028,
  FPTRUNC = 0x029,

  FADD32 = 0x030,
  FSUB32 = 0x031,
  FMUL32 = 0x032,
  FDIV32 = 0x033,
  FNEG32 = 0x034,
  FCMP32 = 0x035, // Aux 是 FloatPredicate，Dst 接收 0 或 1。

  FADD64 = 0x038,
  FSUB64 = 0x039,
  FMUL64 = 0x03A,
  FDIV64 = 0x03B,
  FNEG64 = 0x03C,
  FCMP64 = 0x03D, // Aux 是 FloatPredicate，Dst 接收 0 或 1。

  LOAD8 = 0x040,
  LOAD16 = 0x041,
  LOAD32 = 0x042,
  LOAD64 = 0x043,
  STORE8 = 0x044,
  STORE16 = 0x045,
  STORE32 = 0x046,
  STORE64 = 0x047,

  BR = 0x050,       // PC 相对跳转，Format 必须是 REL32。
  BRCOND = 0x051,   // Src1 != 0 时跳转，Format 必须是 REL32。
  VMCALL = 0x052,   // 保留编号；V1 及后续版本不提供 VM 间调用功能。
  HOSTCALL = 0x053, // Payload 高 8 位 argc、低 24 位 bridge 表索引。

  TRAP = 0xFFE,
  RET = 0xFFF,
};

enum class InstFormat : std::uint8_t {
  RRR = 0, // Dst = op(Src1, Src2)。
  RRI = 1, // Dst = op(Src1, sign_extend(Payload))。

  // LOAD*：Dst = load(Reg[Src1] + sign_extend(Payload))，忽略 Src2。
  MEM_SRC = 2,

  // STORE*：store(Reg[Dst] + sign_extend(Payload), Reg[Src1])，忽略 Src2。
  MEM_DST = 3,

  REL32 = 4,      // BR/BRCOND 使用有符号的指令相对偏移。
  CONST_POOL = 5, // 格式编号保持冻结；LDC 用 Payload 索引 ValueTable。
  CALL = 6,       // HOSTCALL 使用的 index/argc 组合字段。
  NONE = 7,       // NOP/RET/TRAP 没有显式操作数。
};

// 与 LLVM 整数比较谓词对应，可以完整放入 4 位 Aux 字段。
enum class IntPredicate : std::uint8_t {
  EQ = 0,
  NE = 1,
  UGT = 2,
  UGE = 3,
  ULT = 4,
  ULE = 5,
  SGT = 6,
  SGE = 7,
  SLT = 8,
  SLE = 9,
};

// 与 LLVM 的 16 种 FCmp 谓词对应，包括 NaN 的 ordered/unordered 语义。
// 使用 FALSE_VALUE/TRUE_VALUE 命名以避免与平台宏发生冲突。
enum class FloatPredicate : std::uint8_t {
  FALSE_VALUE = 0,
  OEQ = 1,
  OGT = 2,
  OGE = 3,
  OLT = 4,
  OLE = 5,
  ONE = 6,
  ORD = 7,
  UNO = 8,
  UEQ = 9,
  UGT = 10,
  UGE = 11,
  ULT = 12,
  ULE = 13,
  UNE = 14,
  TRUE_VALUE = 15,
};

// 当合法化或类型转换 Opcode 需要指定值宽度时，将该枚举写入 Aux。
enum class ValueWidth : std::uint8_t {
  I1 = 0,
  I8 = 1,
  I16 = 2,
  I32 = 3,
  I64 = 4,
  F32 = 5,
  F64 = 6,
  PTR = 7,
};

// Aux 只有 4 位。浮点转换需要同时表达源/目标宽度，因此不能复用
// ValueWidth。M3 尚不执行浮点转换，但先冻结取值，避免后续破坏指令 ABI。
enum class ConversionMode : std::uint8_t {
  I32_TO_F32 = 0,
  I32_TO_F64 = 1,
  I64_TO_F32 = 2,
  I64_TO_F64 = 3,
  F32_TO_I32 = 4,
  F32_TO_I64 = 5,
  F64_TO_I32 = 6,
  F64_TO_I64 = 7,
  F32_TO_F64 = 8,
  F64_TO_F32 = 9,
};

// 数值是嵌入运行时 ABI 的一部分，新增 Trap 只能追加，不能重排。
enum class VmpTrap : std::uint32_t {
  None = 0,
  InvalidOpcode,
  InvalidFormat,
  InvalidRegister,
  PcOutOfRange,
  BranchOutOfRange,
  StackOverflow,
  StackOutOfRange,
  ConstantOutOfRange,
  DivideByZero,
  SignedDivideOverflow,
  InvalidVmCall, // 稳定保留编号；VMCALL 本身始终按 InvalidOpcode 拒绝。
  InvalidHostCall,
  CallDepthExceeded, // 稳定保留编号；V1 不维护 VM 调用深度。
  ImageIntegrityFailure,
  UnsupportedImageVersion,
  ExplicitTrap,
};

inline constexpr std::uint32_t kMaxFrameSize = 1024U * 1024U;
inline constexpr std::uint32_t kFrameAlignment = 16;
inline constexpr std::uint32_t kMaxArgumentCount = 6;
inline constexpr std::uint32_t kHostCallIndexMask = 0x00FFFFFFU;
inline constexpr unsigned kHostCallArgumentShift = 24;
inline constexpr std::uint32_t kMaxHostCallArgumentCount = 0xFFU;

[[nodiscard]] constexpr std::uint32_t
packHostCallPayload(std::uint32_t FunctionIndex,
                    std::uint32_t ArgumentCount) noexcept {
  return (FunctionIndex & kHostCallIndexMask) |
         ((ArgumentCount & kMaxHostCallArgumentCount)
          << kHostCallArgumentShift);
}

[[nodiscard]] constexpr std::uint32_t
hostCallFunctionIndex(std::uint32_t Payload) noexcept {
  return Payload & kHostCallIndexMask;
}

[[nodiscard]] constexpr std::uint32_t
hostCallArgumentCount(std::uint32_t Payload) noexcept {
  return Payload >> kHostCallArgumentShift;
}

[[nodiscard]] constexpr std::uint32_t
hostCallOverflowSize(std::uint32_t ArgumentCount) noexcept {
  if (ArgumentCount <= kMaxArgumentCount)
    return 0;
  const std::uint32_t Bytes =
      (ArgumentCount - kMaxArgumentCount) * sizeof(std::uint64_t);
  return (Bytes + kFrameAlignment - 1U) & ~(kFrameAlignment - 1U);
}

struct Instruction final {
  Opcode opcode = Opcode::INVALID;
  std::uint8_t aux = 0;
  Reg dst = Reg::ZR;
  Reg src1 = Reg::ZR;
  Reg src2 = Reg::ZR;
  std::uint32_t payload = 0;
  InstFormat format = InstFormat::NONE;
  bool encrypted = false;

  static constexpr std::uint64_t kOpcodeMask = 0x0FFFULL;
  static constexpr std::uint64_t kAuxMask = 0x0FULL;
  static constexpr std::uint64_t kRegisterMask = 0x0FULL;
  static constexpr std::uint64_t kPayloadMask = 0xFFFFFFFFULL;
  static constexpr std::uint64_t kFormatMask = 0x07ULL;

  static constexpr unsigned kOpcodeShift = 52;
  static constexpr unsigned kAuxShift = 48;
  static constexpr unsigned kDstShift = 44;
  static constexpr unsigned kSrc1Shift = 40;
  static constexpr unsigned kSrc2Shift = 36;
  static constexpr unsigned kPayloadShift = 4;
  static constexpr unsigned kFormatShift = 1;

  [[nodiscard]] constexpr std::uint64_t encode() const noexcept {
    return ((static_cast<std::uint64_t>(opcode) & kOpcodeMask)
            << kOpcodeShift) |
           ((static_cast<std::uint64_t>(aux) & kAuxMask) << kAuxShift) |
           ((static_cast<std::uint64_t>(dst) & kRegisterMask) << kDstShift) |
           ((static_cast<std::uint64_t>(src1) & kRegisterMask) << kSrc1Shift) |
           ((static_cast<std::uint64_t>(src2) & kRegisterMask) << kSrc2Shift) |
           ((static_cast<std::uint64_t>(payload) & kPayloadMask)
            << kPayloadShift) |
           ((static_cast<std::uint64_t>(format) & kFormatMask)
            << kFormatShift) |
           static_cast<std::uint64_t>(encrypted);
  }

  // 必须先由字节码层解密 bit 63..1，然后才能调用该函数解码。
  [[nodiscard]] static constexpr Instruction
  decode(std::uint64_t word) noexcept {
    Instruction result;
    result.opcode = static_cast<Opcode>((word >> kOpcodeShift) & kOpcodeMask);
    result.aux = static_cast<std::uint8_t>((word >> kAuxShift) & kAuxMask);
    result.dst = static_cast<Reg>((word >> kDstShift) & kRegisterMask);
    result.src1 = static_cast<Reg>((word >> kSrc1Shift) & kRegisterMask);
    result.src2 = static_cast<Reg>((word >> kSrc2Shift) & kRegisterMask);
    result.payload =
        static_cast<std::uint32_t>((word >> kPayloadShift) & kPayloadMask);
    result.format =
        static_cast<InstFormat>((word >> kFormatShift) & kFormatMask);
    result.encrypted = (word & 1ULL) != 0;
    return result;
  }

  [[nodiscard]] constexpr std::int64_t signedPayload() const noexcept {
    return (payload & 0x80000000U) != 0
               ? static_cast<std::int64_t>(payload) - (1LL << 32)
               : static_cast<std::int64_t>(payload);
  }

  // REL32 以指令数量而不是字节数量为单位，并以当前指令的下一条指令为基准，
  // 因此 Payload 为零时会自然顺序执行下一条指令。
  [[nodiscard]] constexpr std::int64_t branchDeltaBytes() const noexcept {
    return signedPayload() * kInstructionSize;
  }
};

[[nodiscard]] constexpr bool isIntegerWidth(std::uint8_t Aux) noexcept {
  return Aux <= static_cast<std::uint8_t>(ValueWidth::I64);
}

[[nodiscard]] constexpr bool isWritableRegister(Reg R) noexcept {
  // ZR 可作为目的寄存器，写入会被解释器丢弃；只有 SA 禁止被普通指令写入。
  return R != Reg::SA;
}

[[nodiscard]] constexpr bool isCanonicalNone(const Instruction &Inst,
                                             bool AllowPayload) noexcept {
  return Inst.format == InstFormat::NONE && Inst.dst == Reg::ZR &&
         Inst.src1 == Reg::ZR && Inst.src2 == Reg::ZR && Inst.aux == 0 &&
         (AllowPayload || Inst.payload == 0);
}

// 验证 M0-M3 明文整数解释器接受的指令。代码表构建期与运行时调用同一函数，
// 因而不会出现“编译器认为合法、运行时却以另一种格式解码”的漂移。
[[nodiscard]] constexpr VmpTrap
validateM3Instruction(const Instruction &Inst, std::uint32_t Pc,
                      std::uint32_t InstructionCount,
                      std::uint32_t ConstantCount) noexcept {
  if (Inst.encrypted)
    return VmpTrap::InvalidFormat;

  const auto CheckDst = [&]() constexpr {
    return isWritableRegister(Inst.dst) ? VmpTrap::None
                                        : VmpTrap::InvalidRegister;
  };
  const auto IsRRR = [&]() constexpr {
    return Inst.format == InstFormat::RRR && Inst.payload == 0;
  };
  const auto IsIntegerBinary = [&]() constexpr {
    return (Inst.format == InstFormat::RRR || Inst.format == InstFormat::RRI) &&
           isIntegerWidth(Inst.aux) &&
           (Inst.format != InstFormat::RRR || Inst.payload == 0) &&
           (Inst.format != InstFormat::RRI || Inst.src2 == Reg::ZR);
  };

  switch (Inst.opcode) {
  case Opcode::NOP:
    return isCanonicalNone(Inst, false) ? VmpTrap::None
                                        : VmpTrap::InvalidFormat;
  case Opcode::MOV:
    if (!IsRRR() || Inst.src2 != Reg::ZR || Inst.aux != 0)
      return VmpTrap::InvalidFormat;
    return CheckDst();
  case Opcode::LDC:
    if (Inst.format != InstFormat::CONST_POOL || Inst.src1 != Reg::ZR ||
        Inst.src2 != Reg::ZR || Inst.aux != 0)
      return VmpTrap::InvalidFormat;
    if (Inst.payload >= ConstantCount)
      return VmpTrap::ConstantOutOfRange;
    return CheckDst();

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
    if (!IsIntegerBinary())
      return VmpTrap::InvalidFormat;
    return CheckDst();
  case Opcode::NOT:
    if (!IsRRR() || Inst.src2 != Reg::ZR || !isIntegerWidth(Inst.aux))
      return VmpTrap::InvalidFormat;
    return CheckDst();
  case Opcode::ICMP:
    if (!IsRRR() || Inst.aux > static_cast<std::uint8_t>(IntPredicate::SLE))
      return VmpTrap::InvalidFormat;
    return CheckDst();

  case Opcode::TRUNC:
  case Opcode::ZEXT:
  case Opcode::SEXT:
    if (!IsRRR() || Inst.src2 != Reg::ZR || !isIntegerWidth(Inst.aux))
      return VmpTrap::InvalidFormat;
    return CheckDst();
  case Opcode::BITCAST:
    if (!IsRRR() || Inst.src2 != Reg::ZR || Inst.aux != 0)
      return VmpTrap::InvalidFormat;
    return CheckDst();

  case Opcode::LOAD8:
  case Opcode::LOAD16:
  case Opcode::LOAD32:
  case Opcode::LOAD64:
    if (Inst.format != InstFormat::MEM_SRC || Inst.src2 != Reg::ZR ||
        Inst.aux != 0)
      return VmpTrap::InvalidFormat;
    return CheckDst();
  case Opcode::STORE8:
  case Opcode::STORE16:
  case Opcode::STORE32:
  case Opcode::STORE64:
    if (Inst.format != InstFormat::MEM_DST || Inst.src2 != Reg::ZR ||
        Inst.aux != 0 || Inst.dst == Reg::ZR)
      return VmpTrap::InvalidFormat;
    return VmpTrap::None;

  case Opcode::BR:
  case Opcode::BRCOND: {
    if (Inst.format != InstFormat::REL32 || Inst.dst != Reg::ZR ||
        Inst.src2 != Reg::ZR || Inst.aux != 0 ||
        (Inst.opcode == Opcode::BR && Inst.src1 != Reg::ZR))
      return VmpTrap::InvalidFormat;
    const std::int64_t Target =
        static_cast<std::int64_t>(Pc) + 1 + Inst.signedPayload();
    return Target >= 0 && Target < static_cast<std::int64_t>(InstructionCount)
               ? VmpTrap::None
               : VmpTrap::BranchOutOfRange;
  }
  case Opcode::HOSTCALL:
    if (Inst.format != InstFormat::CALL || Inst.dst != Reg::ZR ||
        Inst.src1 != Reg::ZR || Inst.src2 != Reg::ZR || Inst.aux != 0)
      return VmpTrap::InvalidFormat;
    return VmpTrap::None;
  case Opcode::RET:
    return isCanonicalNone(Inst, false) ? VmpTrap::None
                                        : VmpTrap::InvalidFormat;
  case Opcode::TRAP:
    return isCanonicalNone(Inst, true) ? VmpTrap::None : VmpTrap::InvalidFormat;

  // 浮点和 VM 间调用 Opcode 已冻结，但不属于 M3 的执行能力。
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
  case Opcode::INVALID:
    return VmpTrap::InvalidOpcode;
  }
  return VmpTrap::InvalidOpcode;
}

static_assert(std::numeric_limits<std::uint64_t>::digits == 64,
              "VM 指令格式要求 uint64_t 必须正好为 64 位");
static_assert(static_cast<std::uint16_t>(Opcode::RET) <=
                  Instruction::kOpcodeMask,
              "Opcode 必须能够放入 12 位指令字段");
static_assert(static_cast<std::uint8_t>(Reg::ZR) + 1 == kRegisterCount,
              "寄存器枚举必须正好填满 4 位寄存器字段");
} // namespace vllvm::vm
