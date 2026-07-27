#include "VMP.h"

#include "llvm/IR/Function.h"
#include "llvm/IR/Module.h"
#include "llvm/Pass.h"
#include "llvm/Support/Endian.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Transforms/VLLVM/VmpCommon.h"
#include "llvm/Transforms/VLLVM/VmpFunctionCompiler.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>

using namespace llvm;
using namespace ::vllvm::vm;

namespace {

void write16(raw_ostream &Output, std::uint16_t Value) {
  char Bytes[2];
  support::endian::write16le(Bytes, Value);
  Output.write(Bytes, sizeof(Bytes));
}

void write32(raw_ostream &Output, std::uint32_t Value) {
  char Bytes[4];
  support::endian::write32le(Bytes, Value);
  Output.write(Bytes, sizeof(Bytes));
}

void write64(raw_ostream &Output, std::uint64_t Value) {
  char Bytes[8];
  support::endian::write64le(Bytes, Value);
  Output.write(Bytes, sizeof(Bytes));
}

void writeFailure(raw_ostream &Output, StringRef Error) {
  const std::size_t Length = std::min<std::size_t>(
      Error.size(), std::numeric_limits<std::uint32_t>::max());
  write32(Output, kCodegenFailureMagic);
  write32(Output, static_cast<std::uint32_t>(Length));
  Output.write(Error.data(), Length);
}

bool emitModule(Module &M, raw_pwrite_stream &Output) {
  Function *Definition = nullptr;
  for (Function &F : M) {
    if (F.isDeclaration())
      continue;
    if (Definition) {
      writeFailure(Output,
                   "纯 VMP CodeGen 流当前要求 Module 中只有一个函数定义");
      return false;
    }
    Definition = &F;
  }
  if (!Definition) {
    writeFailure(Output, "纯 VMP CodeGen 流没有可编译的函数定义");
    return false;
  }

  llvm::vllvm::VMPCodegenResult Result;
  std::string Error;
  llvm::vllvm::FunctionCompiler Compiler(*Definition);
  if (!Compiler.compile(Result, Error)) {
    writeFailure(Output, Error);
    return false;
  }

  const std::uint64_t TotalSize64 =
      kCodegenHeaderSize + static_cast<std::uint64_t>(
                               Result.Code.size() + Result.ValueTable.size()) *
                               8;
  if (TotalSize64 > std::numeric_limits<std::uint32_t>::max()) {
    writeFailure(Output, "纯 VMP CodeGen 流超过 32 位尺寸上限");
    return false;
  }

  write32(Output, kCodegenStreamMagic);
  write16(Output, kCodegenStreamVersion);
  write16(Output, kCodegenHeaderSize);
  write32(Output, static_cast<std::uint32_t>(TotalSize64));
  write32(Output, static_cast<std::uint32_t>(Result.Code.size()));
  write32(Output, static_cast<std::uint32_t>(Result.ValueTable.size()));
  write32(Output, Result.FrameSize);
  write32(Output, 0); // entryPc
  write32(Output, 0); // flags/reserved
  for (std::uint64_t Word : Result.Code)
    write64(Output, Word);
  for (std::uint64_t Value : Result.ValueTable)
    write64(Output, Value);
  return true;
}

class VMPCodeEmitterPass final : public ModulePass {
public:
  static char ID;

  explicit VMPCodeEmitterPass(raw_pwrite_stream &Output)
      : ModulePass(ID), Output(Output) {}

  bool runOnModule(Module &M) override {
    emitModule(M, Output);
    return false;
  }

  StringRef getPassName() const override {
    return "VMP bytecode stream serializer";
  }

private:
  raw_pwrite_stream &Output;
};

char VMPCodeEmitterPass::ID = 0;

} // namespace

Pass *llvm::createVMPCodeEmitterPass(raw_pwrite_stream &Output) {
  return new VMPCodeEmitterPass(Output);
}
