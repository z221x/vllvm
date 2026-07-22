include "llvm/Target/Target.td"

include "VMPRegisterInfo.td"
include "VMPCallingConv.td"
include "BPF/BPFInstrInfo.td"
include "BPF/GISel/BPFRegisterBanks.td"

def BPFInstrInfo : InstrInfo;

class Proc<string Name, list<SubtargetFeature> Features>
    : Processor<Name, NoItineraries, Features>;
def DummyFeature : SubtargetFeature<"dummy", "isDummyMode", "true",
                                    "unused feature">;
def ALU32 : SubtargetFeature<"alu32", "HasAlu32", "true",
                             "Enable 32-bit instructions">;
def DwarfRIS : SubtargetFeature<"dwarfris", "UseDwarfRIS", "true",
                                "Disable cross-section DWARF relocations">;
def : Proc<"generic", []>;
def : Proc<"v1", []>;
def : Proc<"v2", []>;
def : Proc<"v3", [ALU32]>;
def : Proc<"v4", [ALU32]>;
def : Proc<"probe", []>;

def BPFInstPrinter : AsmWriter {
  string AsmWriterClassName = "InstPrinter";
  bit isMCAsmWriter = 1;
}

def BPF : Target {
  let InstructionSet = BPFInstrInfo;
  let AssemblyWriters = [BPFInstPrinter];
}
