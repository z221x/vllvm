#pragma once

#include "../include/VmpCommon.h"

#include <cstdint>

namespace vllvm::vm {

// codeSize 使用字节数，Code 直接指向第一条 64 位 VM 指令。正常返回 R0；任何
// Trap 都在解释器内部终止，不把错误码暴露到宿主包装函数。
extern "C" std::uint64_t
__vllvm_vmp_execute(const std::uint64_t *Code, std::uint32_t CodeSize,
                    const void *const *FunctionTable,
                    const std::uint64_t *ValueTable, std::uint32_t ValueCount,
                    std::uint64_t *Arguments, std::uint32_t ArgumentCount,
                    std::uint8_t *Stack, std::uint32_t StackSize) noexcept;

#if defined(VLLVM_VMP_TESTING)
// 单元测试使用同一执行核心观察 Trap；该符号不会编进嵌入运行时 bitcode。
extern "C" std::uint32_t __vllvm_vmp_execute_checked(
    const std::uint64_t *Code, std::uint32_t CodeSize,
    const void *const *FunctionTable, const std::uint64_t *ValueTable,
    std::uint32_t ValueCount, std::uint64_t *Arguments,
    std::uint32_t ArgumentCount, std::uint8_t *Stack, std::uint32_t StackSize,
    std::uint64_t *Result) noexcept;
#endif

} // namespace vllvm::vm
