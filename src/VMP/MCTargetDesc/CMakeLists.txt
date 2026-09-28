add_llvm_component_library(LLVMVMPDesc
  VMPMCInit.cpp

  LINK_COMPONENTS
  Support

  ADD_TO_COMPONENT
  VMP
)
