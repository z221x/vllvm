#include "BPF.h"
#include "BPFMCInstLower.h"
#include "MCTargetDesc/BPFInstPrinter.h"
#include "TargetInfo/BPFTargetInfo.h"
#include "llvm/BinaryFormat/ELF.h"
#include "llvm/CodeGen/AsmPrinter.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/InitializePasses.h"
#include "llvm/MC/MCInst.h"
#include "llvm/MC/MCSectionELF.h"
#include "llvm/MC/MCStreamer.h"
#include "llvm/MC/MCSubtargetInfo.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Support/Compiler.h"

using namespace llvm;

namespace {
class BPFAsmPrinter final : public AsmPrinter {
public:
  explicit BPFAsmPrinter(TargetMachine &TM,
                         std::unique_ptr<MCStreamer> Streamer)
      : AsmPrinter(TM, std::move(Streamer), ID) {}

  StringRef getPassName() const override { return "VMP Assembly Printer"; }

  bool doInitialization(Module &M) override {
    const bool Changed = AsmPrinter::doInitialization(M);
    // 即使当前函数没有常量或 spill，也固定保留三个对象节，Pass 因而可以
    // 使用统一的内存对象提取路径。
    MCSectionELF *ConstantSection = OutContext.getELFSection(
        ".vmp.const", ELF::SHT_PROGBITS, ELF::SHF_ALLOC);
    OutStreamer->switchSection(ConstantSection);
    OutStreamer->emitIntValue(0, 1);
    MCSectionELF *MetaSection = OutContext.getELFSection(
        ".vmp.meta", ELF::SHT_PROGBITS, ELF::SHF_ALLOC);
    OutStreamer->switchSection(MetaSection);
    OutStreamer->emitIntValue(0, 1);
    return Changed;
  }

  void emitInstruction(const MachineInstr *MI) override {
    BPF_MC::verifyInstructionPredicates(MI->getOpcode(),
                                        getSubtargetInfo().getFeatureBits());
    MCInst Lowered;
    BPFMCInstLower Lowering(OutContext, *this);
    Lowering.Lower(MI, Lowered);
    EmitToStreamer(*OutStreamer, Lowered);
  }

  static char ID;
};
} // namespace

char BPFAsmPrinter::ID = 0;

INITIALIZE_PASS(BPFAsmPrinter, "bpf-asm-printer", "VMP Assembly Printer", false,
                false)

extern "C" LLVM_ABI LLVM_EXTERNAL_VISIBILITY void
LLVMInitializeVMPAsmPrinter() {
  RegisterAsmPrinter<BPFAsmPrinter> Printer(getTheBPFTarget());
}
