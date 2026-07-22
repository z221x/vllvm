set(CMAKE_EXPORT_COMPILE_COMMANDS ON CACHE BOOL
    "Generate compile_commands.json for clangd" FORCE)

add_llvm_component_library(LLVMVLLVM
  Utils.cpp
  VLLVMAttribute.cpp
  FunctionObfuscationPass.cpp
  BogusControlFlowPass.cpp
  FlattenFuncPass.cpp
  EncryptoStrPass.cpp
  IndirectCallPass.cpp
  IndirectBranchPass.cpp
  LocalVarStructPass.cpp
  PassInfo.cpp
  CryptoUtils.cpp
  VmpPass.cpp
  VmpRuntimeEmbed.cpp
  vminterpreter/VmpRuntimeBitcode.inc

  ADDITIONAL_HEADER_DIRS
  ${LLVM_MAIN_INCLUDE_DIR}/llvm/Transforms/VLLVM

  LINK_COMPONENTS
  Analysis
  AsmPrinter
  CodeGen
  Core
  MC
  Object
  Passes
  Support
  Target
  TransformUtils
  BitReader
  Linker
  VMPCodeGen
)

target_include_directories(LLVMVLLVM
  PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}/include"
)
