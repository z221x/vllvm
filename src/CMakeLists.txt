set(CMAKE_EXPORT_COMPILE_COMMANDS ON CACHE BOOL
    "Generate compile_commands.json for clangd" FORCE)

add_llvm_component_library(LLVMVLLVM
  Utils.cpp
  FunctionObfuscationPass.cpp
  FlattenFuncPass.cpp
  EncryptoStrPass.cpp
  IndirectCallPass.cpp
  IndirectBranchPass.cpp
  LocalVarStructPass.cpp
  PassInfo.cpp
  CryptoUtils.cpp

  ADDITIONAL_HEADER_DIRS
  ${LLVM_MAIN_INCLUDE_DIR}/llvm/Transforms/VLLVM

  LINK_COMPONENTS
  Analysis
  Core
  Passes
  Support
  TransformUtils
)

target_include_directories(LLVMVLLVM
  PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}/include"
)
