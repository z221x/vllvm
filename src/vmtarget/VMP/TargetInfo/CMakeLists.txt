add_llvm_component_library(LLVMVMPInfo
  VMPTargetInfo.cpp

  LINK_COMPONENTS
  MC
  Support

  ADD_TO_COMPONENT
  VMP
)

target_include_directories(LLVMVMPInfo PRIVATE
  "${LLVM_MAIN_SRC_DIR}/lib/Target/BPF"
)
