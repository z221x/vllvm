#include "VmpCommon.h"

#include <cassert>
#include <cstdint>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

using namespace vllvm::vm;

static std::uint16_t read16(const std::vector<std::uint8_t> &Bytes,
                            std::size_t Offset) {
  return static_cast<std::uint16_t>(Bytes[Offset]) |
         (static_cast<std::uint16_t>(Bytes[Offset + 1]) << 8);
}

static std::uint32_t read32(const std::vector<std::uint8_t> &Bytes,
                            std::size_t Offset) {
  std::uint32_t Result = 0;
  for (unsigned I = 0; I != 4; ++I)
    Result |= static_cast<std::uint32_t>(Bytes[Offset + I]) << (I * 8);
  return Result;
}

static std::uint64_t read64(const std::vector<std::uint8_t> &Bytes,
                            std::size_t Offset) {
  std::uint64_t Result = 0;
  for (unsigned I = 0; I != 8; ++I)
    Result |= static_cast<std::uint64_t>(Bytes[Offset + I]) << (I * 8);
  return Result;
}

int main(int Argc, char **Argv) {
  assert(Argc == 3);
  std::ifstream Input(Argv[1], std::ios::binary);
  std::vector<std::uint8_t> Bytes{
      std::istreambuf_iterator<char>(Input), std::istreambuf_iterator<char>()};
  assert(Bytes.size() >= kCodegenHeaderSize);
  assert(read32(Bytes, 0) == kCodegenStreamMagic);
  assert(read16(Bytes, 4) == kCodegenStreamVersion);
  assert(read16(Bytes, 6) == kCodegenHeaderSize);
  assert(read32(Bytes, 8) == Bytes.size());

  const std::uint32_t InstructionCount = read32(Bytes, 12);
  const std::uint32_t ValueCount = read32(Bytes, 16);
  const std::uint32_t FrameSize = read32(Bytes, 20);
  assert(InstructionCount != 0);
  assert(FrameSize <= kMaxFrameSize);
  assert((FrameSize % kFrameAlignment) == 0);
  assert(read32(Bytes, 24) == 0);
  assert(read32(Bytes, 28) == 0);
  assert(kCodegenHeaderSize +
             (static_cast<std::uint64_t>(InstructionCount) +
              static_cast<std::uint64_t>(ValueCount)) *
                 8 ==
         Bytes.size());

  bool HasBranch = false;
  bool HasMemory = false;
  bool HasHostCall = false;
  for (std::uint32_t Pc = 0; Pc != InstructionCount; ++Pc) {
    Instruction Inst =
        Instruction::decode(read64(Bytes, kCodegenHeaderSize + Pc * 8));
    assert(validateM3Instruction(Inst, Pc, InstructionCount, ValueCount) ==
           VmpTrap::None);
    HasBranch |= Inst.opcode == Opcode::BR || Inst.opcode == Opcode::BRCOND;
    HasMemory |=
        (Inst.opcode >= Opcode::LOAD8 && Inst.opcode <= Opcode::STORE64);
    if (Inst.opcode == Opcode::HOSTCALL) {
      HasHostCall = true;
      assert(hostCallFunctionIndex(Inst.payload) == 0);
      assert(hostCallArgumentCount(Inst.payload) == 15);
    }
  }

  const std::string Mode = Argv[2];
  if (Mode == "codegen") {
    assert(HasBranch);
    assert(HasMemory);
    assert(!HasHostCall);
  } else {
    assert(Mode == "hostcall");
    assert(HasHostCall);
  }
  return 0;
}
