#include "BPFTargetMachine.h"
#include "TargetInfo/BPFTargetInfo.h"
#include "llvm/CodeGen/TargetLoweringObjectFileImpl.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Support/Compiler.h"

using namespace llvm;

// LLVM 根据实验 Target 名称寻找 VMP 初始化入口。底层 SelectionDAG 骨架沿用
// BPF 类名，因此这里把标准入口转发到复用实现。
extern "C" LLVM_ABI void LLVMInitializeBPFTarget();

namespace llvm {
namespace {
class VMPTargetMachine final : public BPFTargetMachine {
public:
  VMPTargetMachine(const Target &T, const Triple &RequestedTriple,
                   StringRef CPU, StringRef Features,
                   const TargetOptions &Options, std::optional<Reloc::Model> RM,
                   std::optional<CodeModel::Model> CM, CodeGenOptLevel OL,
                   bool JIT)
      : BPFTargetMachine(T, Triple("bpfel-unknown-none"),
                         CPU.empty() ? "v4" : CPU, Features, Options, RM, CM,
                         OL, JIT),
        HasLittleElfTriple(RequestedTriple.getArch() == Triple::bpfel) {}

  bool
  addPassesToEmitFile(PassManagerBase &PM, raw_pwrite_stream &Output,
                      raw_pwrite_stream *DwoOutput, CodeGenFileType FileType,
                      bool DisableVerify = true,
                      MachineModuleInfoWrapperPass *MMIWP = nullptr) override {
    // Pass 内部总是显式使用 bpfel 临时 Triple。外部 llc 若遗漏该约束，
    // 清晰拒绝输出，不能把 ELF writer 与宿主 Mach-O/COFF streamer 混用。
    if (!HasLittleElfTriple)
      return true;
    return BPFTargetMachine::addPassesToEmitFile(
        PM, Output, DwoOutput, FileType, DisableVerify, MMIWP);
  }

private:
  bool HasLittleElfTriple;
};
} // namespace
} // namespace llvm

extern "C" LLVM_ABI LLVM_EXTERNAL_VISIBILITY void LLVMInitializeVMPTarget() {
  LLVMInitializeBPFTarget();
  // 无公开 VMP Triple；无论宿主 Module 是 Mach-O/ELF/COFF，内部 Target
  // 都规范化为固定的小端 ELF，避免对象 streamer 受宿主格式影响。
  RegisterTargetMachine<VMPTargetMachine> TargetMachine(getTheBPFTarget());
}
