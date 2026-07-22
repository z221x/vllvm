set(BPF_MC_SOURCE_DIR "${LLVM_MAIN_SRC_DIR}/lib/Target/BPF/MCTargetDesc")
add_llvm_component_library(LLVMVMPDesc
  "${BPF_MC_SOURCE_DIR}/BPFMCTargetDesc.cpp"
  "${BPF_MC_SOURCE_DIR}/BPFAsmBackend.cpp"
  "${BPF_MC_SOURCE_DIR}/BPFInstPrinter.cpp"
  "${BPF_MC_SOURCE_DIR}/BPFMCCodeEmitter.cpp"
  "${BPF_MC_SOURCE_DIR}/BPFELFObjectWriter.cpp"
  VMPMCInit.cpp

  LINK_COMPONENTS
  MC
  VMPInfo
  Support
  TargetParser

  ADD_TO_COMPONENT
  VMP
)

target_include_directories(LLVMVMPDesc PRIVATE
  "${LLVM_MAIN_SRC_DIR}/lib/Target/BPF"
  "${LLVM_MAIN_SRC_DIR}/lib/Target/BPF/MCTargetDesc"
  "${CMAKE_CURRENT_BINARY_DIR}/.."
)
