#include "BPFRegisterInfo.h"
#include "BPFSubtarget.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/RegisterScavenging.h"
#include "llvm/CodeGen/TargetFrameLowering.h"
#include "llvm/CodeGen/TargetInstrInfo.h"
#include "llvm/IR/DiagnosticInfo.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/ErrorHandling.h"

#define GET_REGINFO_TARGET_DESC
#include "BPFGenRegisterInfo.inc"

using namespace llvm;

static cl::opt<int>
    VMPStackSizeOption("vmp-stack-size",
                       cl::desc("Specify the VMP codegen stack size limit"),
                       cl::init(1024 * 1024));

BPFRegisterInfo::BPFRegisterInfo() : BPFGenRegisterInfo(BPF::R0) {}

const MCPhysReg *
BPFRegisterInfo::getCalleeSavedRegs(const MachineFunction *) const {
  return CSR_SaveList;
}

const uint32_t *
BPFRegisterInfo::getCallPreservedMask(const MachineFunction &,
                                      CallingConv::ID CC) const {
  return CC == CallingConv::PreserveAll ? CSR_PreserveAll_RegMask : CSR_RegMask;
}

BitVector BPFRegisterInfo::getReservedRegs(const MachineFunction &) const {
  BitVector Reserved(getNumRegs());
  markSuperRegs(Reserved, BPF::W10); // Machine frame pointer
  markSuperRegs(Reserved, BPF::W11); // Machine stack pointer
  markSuperRegs(Reserved, BPF::W12); // M3 branch expansion scratch
  markSuperRegs(Reserved, BPF::W13); // M3 immediate expansion scratch
  return Reserved;
}

static void diagnoseFrameSize(int Offset, MachineFunction &MF, DebugLoc &DL,
                              MachineBasicBlock &MBB) {
  if (Offset > -VMPStackSizeOption)
    return;
  if (!DL)
    for (MachineInstr &I : MBB)
      if (I.getDebugLoc()) {
        DL = I.getDebugLoc();
        break;
      }
  MF.getFunction().getContext().diagnose(DiagnosticInfoUnsupported(
      MF.getFunction(), "VMP stack frame exceeds the configured limit", DL));
}

bool BPFRegisterInfo::eliminateFrameIndex(MachineBasicBlock::iterator II,
                                          int SPAdj, unsigned,
                                          RegScavenger *) const {
  assert(SPAdj == 0 && "unexpected stack adjustment");
  MachineInstr &MI = *II;
  unsigned Operand = 0;
  while (!MI.getOperand(Operand).isFI()) {
    ++Operand;
    assert(Operand < MI.getNumOperands() && "missing frame index operand");
  }

  MachineBasicBlock &MBB = *MI.getParent();
  MachineFunction &MF = *MBB.getParent();
  DebugLoc DL = MI.getDebugLoc();
  const int FrameIndex = MI.getOperand(Operand).getIndex();
  const TargetInstrInfo &TII = *MF.getSubtarget().getInstrInfo();

  if (MI.getOpcode() == BPF::MOV_rr) {
    const int Offset = MF.getFrameInfo().getObjectOffset(FrameIndex);
    diagnoseFrameSize(Offset, MF, DL, MBB);
    MI.getOperand(Operand).ChangeToRegister(BPF::R10, false);
    Register Dst = MI.getOperand(Operand - 1).getReg();
    BuildMI(MBB, ++II, DL, TII.get(BPF::ADD_ri), Dst)
        .addReg(Dst)
        .addImm(Offset);
    return false;
  }

  const int Offset = MF.getFrameInfo().getObjectOffset(FrameIndex) +
                     MI.getOperand(Operand + 1).getImm();
  if (!isInt<32>(Offset))
    llvm_unreachable("VMP frame offset does not fit REL32");
  diagnoseFrameSize(Offset, MF, DL, MBB);

  if (MI.getOpcode() == BPF::FI_ri) {
    Register Dst = MI.getOperand(Operand - 1).getReg();
    BuildMI(MBB, ++II, DL, TII.get(BPF::MOV_rr), Dst).addReg(BPF::R10);
    BuildMI(MBB, II, DL, TII.get(BPF::ADD_ri), Dst).addReg(Dst).addImm(Offset);
    MI.eraseFromParent();
  } else {
    MI.getOperand(Operand).ChangeToRegister(BPF::R10, false);
    MI.getOperand(Operand + 1).ChangeToImmediate(Offset);
  }
  return false;
}

Register BPFRegisterInfo::getFrameRegister(const MachineFunction &) const {
  return BPF::R10;
}
