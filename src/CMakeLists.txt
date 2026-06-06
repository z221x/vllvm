cmake_minimum_required(VERSION 3.14)
project(vollvm_pass)
find_package(LLVM REQUIRED CONFIG
    PATHS "../../install"
    NO_DEFAULT_PATH
)
list(APPEND CMAKE_MODULE_PATH "${LLVM_CMAKE_DIR}")
include(AddLLVM)
include_directories("./include") # 包含 ./include 文件夹中的头文件
separate_arguments(LLVM_DEFINITIONS_LIST NATIVE_COMMAND ${LLVM_DEFINITIONS})
add_definitions(${LLVM_DEFINITIONS_LIST})
include_directories(${LLVM_INCLUDE_DIRS})
add_llvm_library(
    vollvm MODULE
    Utils.cpp
    FlattenFuncPass.cpp
    EncryptoStrPass.cpp
    IndirectCallPass.cpp
    IndirectBranchPass.cpp
    PassInfo.cpp
    CryptoUtils.cpp
)