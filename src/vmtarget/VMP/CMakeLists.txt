# M2 先复用 LLVM 21 已验证的 BPF SelectionDAG/寄存器分配骨架。BPF 只作为
# 编译器内部 Machine IR，不会进入最终程序；VmpPass 产出的仍是 VMP ISA。
add_llvm_component_group(VMP)

set(BPF_SOURCE_DIR "${LLVM_MAIN_SRC_DIR}/lib/Target/BPF")
list(APPEND LLVM_TABLEGEN_FLAGS "-I${BPF_SOURCE_DIR}")
set(LLVM_TARGET_DEFINITIONS VMP.td)

tablegen(LLVM BPFGenAsmWriter.inc -gen-asm-writer)
tablegen(LLVM BPFGenCallingConv.inc -gen-callingconv)
tablegen(LLVM BPFGenDAGISel.inc -gen-dag-isel)
tablegen(LLVM BPFGenInstrInfo.inc -gen-instr-info)
tablegen(LLVM BPFGenMCCodeEmitter.inc -gen-emitter)
tablegen(LLVM BPFGenRegisterInfo.inc -gen-register-info)
tablegen(LLVM BPFGenSubtargetInfo.inc -gen-subtarget)
tablegen(LLVM BPFGenGlobalISel.inc -gen-global-isel)
tablegen(LLVM BPFGenRegisterBank.inc -gen-register-bank)
add_public_tablegen_target(VMPCommonTableGen)

add_llvm_target(VMPCodeGen
  "${BPF_SOURCE_DIR}/GISel/BPFCallLowering.cpp"
  "${BPF_SOURCE_DIR}/GISel/BPFInstructionSelector.cpp"
  "${BPF_SOURCE_DIR}/GISel/BPFRegisterBankInfo.cpp"
  "${BPF_SOURCE_DIR}/GISel/BPFLegalizerInfo.cpp"
  "${BPF_SOURCE_DIR}/BPFAbstractMemberAccess.cpp"
  "${BPF_SOURCE_DIR}/BPFAdjustOpt.cpp"
  "${BPF_SOURCE_DIR}/BPFASpaceCastSimplifyPass.cpp"
  "${BPF_SOURCE_DIR}/BPFCheckAndAdjustIR.cpp"
  "${BPF_SOURCE_DIR}/BPFFrameLowering.cpp"
  "${BPF_SOURCE_DIR}/BPFInstrInfo.cpp"
  "${BPF_SOURCE_DIR}/BPFIRPeephole.cpp"
  "${BPF_SOURCE_DIR}/BPFISelDAGToDAG.cpp"
  "${BPF_SOURCE_DIR}/BPFISelLowering.cpp"
  "${BPF_SOURCE_DIR}/BPFMCInstLower.cpp"
  "${BPF_SOURCE_DIR}/BPFPreserveDIType.cpp"
  "${BPF_SOURCE_DIR}/BPFPreserveStaticOffset.cpp"
  VMPRegisterInfo.cpp
  "${BPF_SOURCE_DIR}/BPFSelectionDAGInfo.cpp"
  "${BPF_SOURCE_DIR}/BPFSubtarget.cpp"
  "${BPF_SOURCE_DIR}/BPFTargetMachine.cpp"
  "${BPF_SOURCE_DIR}/BPFMIPeephole.cpp"
  "${BPF_SOURCE_DIR}/BPFMIChecking.cpp"
  "${BPF_SOURCE_DIR}/BPFMISimplifyPatchable.cpp"
  "${BPF_SOURCE_DIR}/BTFDebug.cpp"
  VMPAsmPrinter.cpp
  VMPInit.cpp

  LINK_COMPONENTS
  Analysis
  AsmPrinter
  VMPDesc
  VMPInfo
  CodeGen
  CodeGenTypes
  Core
  GlobalISel
  IPO
  MC
  Passes
  Scalar
  SelectionDAG
  Support
  Target
  TargetParser
  TransformUtils

  ADD_TO_COMPONENT
  VMP
)

target_include_directories(LLVMVMPCodeGen PRIVATE
  "${BPF_SOURCE_DIR}"
  "${CMAKE_CURRENT_BINARY_DIR}"
)

# 共享 BPF SelectionDAG 实现时只为 VMP 启用 R0-R5 六参数调用约定。
target_compile_definitions(LLVMVMPCodeGen PRIVATE VLLVM_VMP_TARGET=1)

add_subdirectory(MCTargetDesc)
add_subdirectory(TargetInfo)
