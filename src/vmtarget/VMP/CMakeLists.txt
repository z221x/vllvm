add_llvm_component_group(VMP)

add_llvm_target(VMPCodeGen
  VMPCodeGenerator.cpp
  VMPFunctionCompiler.cpp
  VMPInit.cpp

  LINK_COMPONENTS
  Core
  Support
  Target
  VMPDesc
  VMPInfo

  ADD_TO_COMPONENT
  VMP
)

add_subdirectory(MCTargetDesc)
add_subdirectory(TargetInfo)
