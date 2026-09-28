add_llvm_component_library(LLVMVMPInfo
  VMPTargetInfo.cpp

  LINK_COMPONENTS
  MC
  Support

  ADD_TO_COMPONENT
  VMP
)
