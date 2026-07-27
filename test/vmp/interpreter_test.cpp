#include "../../src/vminterpreter/interpreter.h"

#include <cassert>
#include <cstdint>
#include <limits>
#include <vector>

using namespace vllvm::vm;

namespace {

struct Program {
  std::vector<std::uint64_t> Code;
  std::vector<std::uint64_t> Values;
};

Program makeImage(const std::vector<Instruction> &Code,
                  const std::vector<std::uint64_t> &Constants, std::uint32_t,
                  std::uint32_t = 0) {
  Program Result;
  Result.Code.reserve(Code.size());
  for (const Instruction &Inst : Code)
    Result.Code.push_back(Inst.encode());
  Result.Values = Constants;
  return Result;
}

std::uint32_t run(const Program &Image,
                  const std::vector<std::uint64_t> &Arguments,
                  std::uint64_t &Result, std::uint8_t *Stack = nullptr,
                  std::uint32_t StackSize = 0,
                  const std::vector<const void *> &FunctionTable = {}) {
  std::vector<std::uint64_t> MutableArguments = Arguments;
  return __vllvm_vmp_execute_checked(
      Image.Code.data(), Image.Code.size() * sizeof(std::uint64_t),
      FunctionTable.empty() ? nullptr : FunctionTable.data(),
      Image.Values.empty() ? nullptr : Image.Values.data(), Image.Values.size(),
      MutableArguments.empty() ? nullptr : MutableArguments.data(),
      MutableArguments.size(), Stack, StackSize, &Result);
}

std::uint64_t testHostTarget(std::uint64_t A0, std::uint64_t A1,
                             std::uint64_t A2, std::uint64_t A3,
                             std::uint64_t A4, std::uint64_t A5,
                             std::uint64_t A6, std::uint64_t A7) {
  return A0 + A1 * 2 + A2 * 3 + A3 * 4 + A4 * 5 + A5 * 6 + A6 * 7 +
         A7 * 8;
}

std::uint64_t
testHostTargetFifteen(std::uint64_t A0, std::uint64_t A1, std::uint64_t A2,
                      std::uint64_t A3, std::uint64_t A4, std::uint64_t A5,
                      std::uint64_t A6, std::uint64_t A7, std::uint64_t A8,
                      std::uint64_t A9, std::uint64_t A10, std::uint64_t A11,
                      std::uint64_t A12, std::uint64_t A13,
                      std::uint64_t A14) {
  return A0 + A1 * 2 + A2 * 3 + A3 * 4 + A4 * 5 + A5 * 6 + A6 * 7 +
         A7 * 8 + A8 * 9 + A9 * 10 + A10 * 11 + A11 * 12 + A12 * 13 +
         A13 * 14 + A14 * 15;
}

Instruction ret() { return {.opcode = Opcode::RET}; }

void testEncodeDecode() {
  Instruction Original{.opcode = Opcode::ADD,
                       .aux = static_cast<std::uint8_t>(ValueWidth::I32),
                       .dst = Reg::R7,
                       .src1 = Reg::R2,
                       .src2 = Reg::R5,
                       .payload = 0,
                       .format = InstFormat::RRR};
  const Instruction Decoded = Instruction::decode(Original.encode());
  assert(Decoded.opcode == Original.opcode);
  assert(Decoded.aux == Original.aux);
  assert(Decoded.dst == Original.dst);
  assert(Decoded.src1 == Original.src1);
  assert(Decoded.src2 == Original.src2);
  assert(Decoded.format == Original.format);
  const std::uint32_t HostPayload = packHostCallPayload(0x123456U, 15);
  assert(hostCallFunctionIndex(HostPayload) == 0x123456U);
  assert(hostCallArgumentCount(HostPayload) == 15);
  assert(kMaxHostCallArgumentCount == 15);
  assert(hostCallOverflowSize(6) == 0);
  assert(hostCallOverflowSize(7) == 16);
  assert(hostCallOverflowSize(8) == 16);
  assert(hostCallOverflowSize(15) == 80);
}

void testArithmeticAndConstantPool() {
  std::vector<Instruction> Code = {
      {.opcode = Opcode::LDC,
       .dst = Reg::R2,
       .payload = 0,
       .format = InstFormat::CONST_POOL},
      {.opcode = Opcode::MUL,
       .aux = static_cast<std::uint8_t>(ValueWidth::I64),
       .dst = Reg::R0,
       .src1 = Reg::R0,
       .src2 = Reg::R2,
       .format = InstFormat::RRR},
      {.opcode = Opcode::ADD,
       .aux = static_cast<std::uint8_t>(ValueWidth::I64),
       .dst = Reg::R0,
       .src1 = Reg::R0,
       .src2 = Reg::ZR,
       .payload = 7,
       .format = InstFormat::RRI},
      ret(),
  };
  auto Image = makeImage(Code, {0x100000001ULL}, 1);
  std::uint64_t Result = 0;
  assert(run(Image, {3}, Result) == 0);
  assert(Result == 0x30000000AULL);
}

std::uint64_t runBinary(Opcode Op, std::uint64_t Lhs, std::uint64_t Rhs,
                        ValueWidth Width = ValueWidth::I64) {
  std::vector<Instruction> Code = {
      {.opcode = Op,
       .aux = static_cast<std::uint8_t>(Width),
       .dst = Reg::R0,
       .src1 = Reg::R0,
       .src2 = Reg::R1,
       .format = InstFormat::RRR},
      ret(),
  };
  auto Image = makeImage(Code, {}, 2);
  std::uint64_t Result = 0;
  assert(run(Image, {Lhs, Rhs}, Result) == 0);
  return Result;
}

void testIntegerHandlers() {
  assert(runBinary(Opcode::ADD, 7, 5) == 12);
  assert(runBinary(Opcode::SUB, 7, 5) == 2);
  assert(runBinary(Opcode::MUL, 7, 5) == 35);
  assert(runBinary(Opcode::UDIV, 19, 4) == 4);
  assert(runBinary(Opcode::UREM, 19, 4) == 3);
  assert(static_cast<std::int64_t>(runBinary(
             Opcode::SDIV, static_cast<std::uint64_t>(-19), 4)) == -4);
  assert(static_cast<std::int64_t>(runBinary(
             Opcode::SREM, static_cast<std::uint64_t>(-19), 4)) == -3);
  assert(runBinary(Opcode::AND, 0xCA, 0xAC) == 0x88);
  assert(runBinary(Opcode::OR, 0xCA, 0xAC) == 0xEE);
  assert(runBinary(Opcode::XOR, 0xCA, 0xAC) == 0x66);
  assert(runBinary(Opcode::SHL, 3, 5) == 96);
  assert(runBinary(Opcode::LSHR, 0x80, 3) == 0x10);
  assert(static_cast<std::int64_t>(runBinary(
             Opcode::ASHR, static_cast<std::uint64_t>(-64), 3)) == -8);

  std::vector<Instruction> Unary = {
      {.opcode = Opcode::NOT,
       .aux = static_cast<std::uint8_t>(ValueWidth::I8),
       .dst = Reg::R0,
       .src1 = Reg::R0,
       .format = InstFormat::RRR},
      ret(),
  };
  auto Image = makeImage(Unary, {}, 1);
  std::uint64_t Result = 0;
  assert(run(Image, {0xA5}, Result) == 0 && Result == 0x5A);
}

void testConversionsAndZeroRegister() {
  std::vector<Instruction> Code = {
      {.opcode = Opcode::TRUNC,
       .aux = static_cast<std::uint8_t>(ValueWidth::I8),
       .dst = Reg::R1,
       .src1 = Reg::R0,
       .format = InstFormat::RRR},
      {.opcode = Opcode::ZEXT,
       .aux = static_cast<std::uint8_t>(ValueWidth::I8),
       .dst = Reg::R2,
       .src1 = Reg::R1,
       .format = InstFormat::RRR},
      {.opcode = Opcode::SEXT,
       .aux = static_cast<std::uint8_t>(ValueWidth::I8),
       .dst = Reg::R3,
       .src1 = Reg::R1,
       .format = InstFormat::RRR},
      {.opcode = Opcode::BITCAST,
       .dst = Reg::R0,
       .src1 = Reg::R3,
       .format = InstFormat::RRR},
      {.opcode = Opcode::MOV,
       .dst = Reg::ZR,
       .src1 = Reg::R2,
       .format = InstFormat::RRR},
      ret(),
  };
  auto Image = makeImage(Code, {}, 1);
  std::uint64_t Result = 0;
  assert(run(Image, {0x1FF}, Result) == 0);
  assert(Result == std::numeric_limits<std::uint64_t>::max());

  std::vector<Instruction> Zero = {
      {.opcode = Opcode::MOV,
       .dst = Reg::ZR,
       .src1 = Reg::R0,
       .format = InstFormat::RRR},
      {.opcode = Opcode::MOV,
       .dst = Reg::R0,
       .src1 = Reg::ZR,
       .format = InstFormat::RRR},
      ret(),
  };
  Image = makeImage(Zero, {}, 1);
  assert(run(Image, {123}, Result) == 0 && Result == 0);
}

void testBranchAndCompare() {
  std::vector<Instruction> Code = {
      {.opcode = Opcode::ICMP,
       .aux = static_cast<std::uint8_t>(IntPredicate::SLT),
       .dst = Reg::R2,
       .src1 = Reg::R0,
       .src2 = Reg::R1,
       .format = InstFormat::RRR},
      {.opcode = Opcode::BRCOND,
       .src1 = Reg::R2,
       .payload = 2,
       .format = InstFormat::REL32},
      {.opcode = Opcode::MOV,
       .dst = Reg::R0,
       .src1 = Reg::R1,
       .format = InstFormat::RRR},
      ret(),
      ret(),
  };
  auto Image = makeImage(Code, {}, 2);
  std::uint64_t Result = 0;
  assert(run(Image, {4, 9}, Result) == 0 && Result == 4);
  assert(run(Image, {12, 9}, Result) == 0 && Result == 9);

  std::vector<Instruction> Loop = {
      {.opcode = Opcode::MOV,
       .dst = Reg::R2,
       .src1 = Reg::ZR,
       .format = InstFormat::RRR},
      {.opcode = Opcode::ADD,
       .aux = static_cast<std::uint8_t>(ValueWidth::I64),
       .dst = Reg::R2,
       .src1 = Reg::R2,
       .payload = 1,
       .format = InstFormat::RRI},
      {.opcode = Opcode::ICMP,
       .aux = static_cast<std::uint8_t>(IntPredicate::ULT),
       .dst = Reg::R3,
       .src1 = Reg::R2,
       .src2 = Reg::R0,
       .format = InstFormat::RRR},
      {.opcode = Opcode::BRCOND,
       .src1 = Reg::R3,
       .payload = static_cast<std::uint32_t>(-3),
       .format = InstFormat::REL32},
      {.opcode = Opcode::MOV,
       .dst = Reg::R0,
       .src1 = Reg::R2,
       .format = InstFormat::RRR},
      ret(),
  };
  Image = makeImage(Loop, {}, 1);
  assert(run(Image, {17}, Result) == 0 && Result == 17);
}

void testStackAndHostMemory() {
  std::vector<Instruction> StackCode = {
      {.opcode = Opcode::STORE64,
       .dst = Reg::SA,
       .src1 = Reg::R0,
       .payload = 8,
       .format = InstFormat::MEM_DST},
      {.opcode = Opcode::LOAD64,
       .dst = Reg::R0,
       .src1 = Reg::SA,
       .payload = 8,
       .format = InstFormat::MEM_SRC},
      ret(),
  };
  auto StackImage = makeImage(StackCode, {}, 1, 16);
  alignas(16) std::uint8_t Stack[16]{};
  std::uint64_t Result = 0;
  assert(run(StackImage, {0xAABBCCDDEEFF0011ULL}, Result, Stack,
             sizeof(Stack)) == 0);
  assert(Result == 0xAABBCCDDEEFF0011ULL);

  std::uint32_t HostValue = 0x12345678U;
  std::vector<Instruction> HostCode = {
      {.opcode = Opcode::LOAD32,
       .dst = Reg::R0,
       .src1 = Reg::R0,
       .format = InstFormat::MEM_SRC},
      ret(),
  };
  auto HostImage = makeImage(HostCode, {}, 1);
  assert(run(HostImage,
             {static_cast<std::uint64_t>(
                 reinterpret_cast<std::uintptr_t>(&HostValue))},
             Result) == 0);
  assert(Result == HostValue);

  std::uint64_t HostWide = 0;
  std::vector<Instruction> WidthCode = {
      {.opcode = Opcode::STORE8,
       .dst = Reg::R0,
       .src1 = Reg::R1,
       .format = InstFormat::MEM_DST},
      {.opcode = Opcode::STORE16,
       .dst = Reg::R0,
       .src1 = Reg::R1,
       .payload = 1,
       .format = InstFormat::MEM_DST},
      {.opcode = Opcode::STORE32,
       .dst = Reg::R0,
       .src1 = Reg::R1,
       .payload = 3,
       .format = InstFormat::MEM_DST},
      {.opcode = Opcode::LOAD64,
       .dst = Reg::R0,
       .src1 = Reg::R0,
       .format = InstFormat::MEM_SRC},
      ret(),
  };
  auto WidthImage = makeImage(WidthCode, {}, 2);
  assert(
      run(WidthImage,
          {reinterpret_cast<std::uintptr_t>(&HostWide), 0x1122334455667788ULL},
          Result) == 0);
  assert((Result & 0x00FFFFFFFFFFFFFFULL) == 0x0055667788778888ULL);
}

void testHostCall() {
  Program HostProgram = makeImage({{.opcode = Opcode::HOSTCALL,
                                    .payload = packHostCallPayload(0, 8),
                                    .format = InstFormat::CALL},
                                   ret()},
                                  {}, 6, 16);
  alignas(16) std::uint8_t Stack[16]{};
  auto *StackArguments = reinterpret_cast<std::uint64_t *>(Stack);
  StackArguments[0] = 7;
  StackArguments[1] = 8;
  std::vector<const void *> FunctionTable = {
      reinterpret_cast<const void *>(&testHostTarget)};
  std::uint64_t Result = 0;
  assert(run(HostProgram, {1, 2, 3, 4, 5, 6}, Result, Stack, sizeof(Stack),
             FunctionTable) == 0);
  assert(Result == 1 + 4 + 9 + 16 + 25 + 36 + 49 + 64);

  assert(run(HostProgram, {1, 2, 3, 4, 5, 6}, Result, Stack, sizeof(Stack)) ==
         static_cast<std::uint32_t>(VmpTrap::InvalidHostCall));
  std::vector<const void *> NullFunctionTable = {nullptr};
  assert(run(HostProgram, {1, 2, 3, 4, 5, 6}, Result, Stack, sizeof(Stack),
             NullFunctionTable) ==
         static_cast<std::uint32_t>(VmpTrap::InvalidHostCall));
  assert(
      run(HostProgram, {1, 2, 3, 4, 5, 6}, Result, Stack, 8, FunctionTable) ==
      static_cast<std::uint32_t>(VmpTrap::StackOutOfRange));

  Program FifteenProgram = makeImage(
      {{.opcode = Opcode::HOSTCALL,
        .payload = packHostCallPayload(0, 15),
        .format = InstFormat::CALL},
       ret()},
      {}, 6, 80);
  alignas(16) std::uint8_t FifteenStack[80]{};
  auto *FifteenStackArguments =
      reinterpret_cast<std::uint64_t *>(FifteenStack);
  for (std::uint64_t I = 0; I != 9; ++I)
    FifteenStackArguments[I] = I + 7;
  std::vector<const void *> FifteenFunctionTable = {
      reinterpret_cast<const void *>(&testHostTargetFifteen)};
  assert(run(FifteenProgram, {1, 2, 3, 4, 5, 6}, Result, FifteenStack,
             sizeof(FifteenStack), FifteenFunctionTable) == 0);
  assert(Result == 1240);

  Program TooManyArguments = makeImage(
      {{.opcode = Opcode::HOSTCALL,
        .payload = 16U << kHostCallArgumentShift,
        .format = InstFormat::CALL},
       ret()},
      {}, 6, 80);
  assert(run(TooManyArguments, {1, 2, 3, 4, 5, 6}, Result, FifteenStack,
             sizeof(FifteenStack), FifteenFunctionTable) ==
         static_cast<std::uint32_t>(VmpTrap::InvalidFormat));
}

void testTraps() {
  std::vector<Instruction> Divide = {
      {.opcode = Opcode::UDIV,
       .aux = static_cast<std::uint8_t>(ValueWidth::I64),
       .dst = Reg::R0,
       .src1 = Reg::R0,
       .src2 = Reg::R1,
       .format = InstFormat::RRR},
      ret(),
  };
  auto Image = makeImage(Divide, {}, 2);
  std::uint64_t Result = 0;
  assert(run(Image, {7, 0}, Result) ==
         static_cast<std::uint32_t>(VmpTrap::DivideByZero));

  std::uint64_t Arguments[] = {7, 1};
  assert(__vllvm_vmp_execute_checked(
             Image.Code.data(), Image.Code.size() * sizeof(std::uint64_t) - 1,
             nullptr, nullptr, 0, Arguments, 2, nullptr, 0,
             &Result) == static_cast<std::uint32_t>(VmpTrap::InvalidFormat));

  std::vector<Instruction> SignedOverflow = {
      {.opcode = Opcode::SDIV,
       .aux = static_cast<std::uint8_t>(ValueWidth::I64),
       .dst = Reg::R0,
       .src1 = Reg::R0,
       .src2 = Reg::R1,
       .format = InstFormat::RRR},
      ret(),
  };
  Image = makeImage(SignedOverflow, {}, 2);
  assert(
      run(Image,
          {static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::min()),
           static_cast<std::uint64_t>(-1)},
          Result) == static_cast<std::uint32_t>(VmpTrap::SignedDivideOverflow));

  Image = makeImage({{.opcode = Opcode::TRAP, .payload = 9}}, {}, 0);
  assert(run(Image, {}, Result) ==
         static_cast<std::uint32_t>(VmpTrap::ExplicitTrap));

  Image = makeImage({{.opcode = Opcode::NOP}}, {}, 0);
  assert(run(Image, {}, Result) ==
         static_cast<std::uint32_t>(VmpTrap::PcOutOfRange));

  Image = makeImage(
      {{.opcode = Opcode::BR, .payload = 5, .format = InstFormat::REL32}}, {},
      0);
  assert(run(Image, {}, Result) ==
         static_cast<std::uint32_t>(VmpTrap::BranchOutOfRange));

  Image = makeImage({{.opcode = Opcode::LDC,
                      .dst = Reg::R0,
                      .payload = 1,
                      .format = InstFormat::CONST_POOL},
                     ret()},
                    {7}, 0);
  assert(run(Image, {}, Result) ==
         static_cast<std::uint32_t>(VmpTrap::ConstantOutOfRange));

  Image = makeImage({{.opcode = Opcode::MOV,
                      .dst = Reg::SA,
                      .src1 = Reg::R0,
                      .format = InstFormat::RRR},
                     ret()},
                    {}, 1);
  assert(run(Image, {1}, Result) ==
         static_cast<std::uint32_t>(VmpTrap::InvalidRegister));

  Image = makeImage({{.opcode = Opcode::LOAD64,
                      .dst = Reg::R0,
                      .src1 = Reg::SA,
                      .payload = 9,
                      .format = InstFormat::MEM_SRC},
                     ret()},
                    {}, 0, 16);
  alignas(16) std::uint8_t Stack[16]{};
  assert(run(Image, {}, Result, Stack, sizeof(Stack)) ==
         static_cast<std::uint32_t>(VmpTrap::StackOutOfRange));
  assert(run(Image, {}, Result, Stack, 8) ==
         static_cast<std::uint32_t>(VmpTrap::StackOutOfRange));
  assert(__vllvm_vmp_execute_checked(
             Image.Code.data(), Image.Code.size() * sizeof(std::uint64_t),
             nullptr, nullptr, 0, nullptr, 0, Stack, kMaxFrameSize + 1,
             &Result) == static_cast<std::uint32_t>(VmpTrap::StackOverflow));

  Image = makeImage({{.opcode = Opcode::FADD32}, ret()}, {}, 0);
  assert(run(Image, {}, Result) ==
         static_cast<std::uint32_t>(VmpTrap::InvalidOpcode));

  Image = makeImage({{.opcode = Opcode::MOV,
                      .dst = Reg::R0,
                      .src1 = Reg::R0,
                      .format = InstFormat::NONE},
                     ret()},
                    {}, 1);
  assert(run(Image, {1}, Result) ==
         static_cast<std::uint32_t>(VmpTrap::InvalidFormat));

  Image = makeImage({ret()}, {}, 0);
  assert(__vllvm_vmp_execute_checked(
             Image.Code.data(), Image.Code.size() * sizeof(std::uint64_t),
             nullptr, nullptr, 1, nullptr, 0, nullptr, 0,
             &Result) == static_cast<std::uint32_t>(VmpTrap::InvalidFormat));

  Image = makeImage(
      {{.opcode = Opcode::VMCALL, .format = InstFormat::CALL}, ret()}, {}, 0);
  assert(run(Image, {}, Result) ==
         static_cast<std::uint32_t>(VmpTrap::InvalidOpcode));
}

} // namespace

int main() {
  testEncodeDecode();
  testArithmeticAndConstantPool();
  testIntegerHandlers();
  testConversionsAndZeroRegister();
  testBranchAndCompare();
  testStackAndHostMemory();
  testHostCall();
  testTraps();
  return 0;
}
