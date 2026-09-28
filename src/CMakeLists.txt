set(CMAKE_EXPORT_COMPILE_COMMANDS ON CACHE BOOL
    "Generate compile_commands.json for clangd" FORCE)

add_llvm_component_library(LLVMVLLVM
  Pass/BB2FuncPass.cpp
  Pass/BogusControlFlowPass.cpp
  Pass/EncryptoStrPass.cpp
  Pass/FlattenFuncPass.cpp
  Pass/IndirectBranchPass.cpp
  Pass/IndirectCallPass.cpp
  Pass/MergePass.cpp
  Pass/PassInfo.cpp
  Pass/VMFlattenFuncPass.cpp
  Pass/VmpPass.cpp
  attribute/VLLVMAttribute.cpp
  common/CryptoUtils.cpp
  common/Utils.cpp
  config/VLLVMConfig.cpp
  # 构建脚本把 src/VMP/VmpRuntimeEmbed.cpp 同步到本目录根，
  # 使其 quote-include 的 "c_func/..." 相对路径成立。
  VmpRuntimeEmbed.cpp
  c_func/vminterpreter/VmpRuntimeBitcode.inc

  ADDITIONAL_HEADER_DIRS
  ${LLVM_MAIN_INCLUDE_DIR}/llvm/Transforms/VLLVM

  LINK_COMPONENTS
  Analysis
  Core
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
  "${CMAKE_CURRENT_SOURCE_DIR}"
  "${CMAKE_CURRENT_SOURCE_DIR}/include"
)
