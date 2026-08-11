#include "funccaller.h"

#include <cstddef>

static_assert(sizeof(func_group) == 16,
              "AArch64 call_func requires a 16-byte func_group");
static_assert(offsetof(func_group, func_ptr_array) == 8,
              "AArch64 call_func requires func_ptr_array at offset 8");

extern "C" {
// 给裸汇编提供稳定的数据区首地址，避免依赖 std::vector 的对象布局。
__attribute__((visibility("hidden"))) func_group *vllvm_func_pool_data =
    nullptr;
}

__attribute__((constructor)) void init_func_caller() {
  create_func_pool(group_count, group_length);
}
// vllvm 去完成
__attribute__((constructor)) void register_funcs() {}
void create_func_pool(int group_count, int group_length) {
  func_pool.reserve(group_count);
  for (int i = 0; i < group_count; ++i) {
    func_pool.emplace_back(group_length);
  }
  vllvm_func_pool_data = func_pool.data();
}
void register_func(int func_group_id, int func_index, void *func_ptr) {
  func_pool[func_group_id].func_ptr_array[func_index] = func_ptr;
  return;
}
#if defined(__aarch64__)
__attribute__((naked)) void call_func(int index) {
#if defined(__APPLE__)
  asm volatile("ubfx w16, w19, #16, #8\n"
               "and w17, w19, #0xff\n"
               "adrp x9, _vllvm_func_pool_data@PAGE\n"
               "ldr x9, [x9, _vllvm_func_pool_data@PAGEOFF]\n"
               "add x9, x9, x16, lsl #4\n"
               "ldr x9, [x9, #8]\n"
               "ldr x19, [x9, x17, lsl #3]\n"
               "ret x19\n");
#else
  asm volatile("ubfx w16, w19, #16, #8\n"
               "and w17, w19, #0xff\n"
               "adrp x9, vllvm_func_pool_data\n"
               "ldr x9, [x9, :lo12:vllvm_func_pool_data]\n"
               "add x9, x9, x16, lsl #4\n"
               "ldr x9, [x9, #8]\n"
               "ldr x19, [x9, x17, lsl #3]\n"
               "ret x19\n");
#endif
}
#endif
