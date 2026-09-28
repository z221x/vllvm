/* 测试脚手架：宿主平台没有 mmap（如 Windows）时用系统页分配器提供符号，
 * 让执行型测试可以在本地验证池语义；Android/Linux 目标走真正的 mmap，
 * 不参与产品构建。 */
#if defined(_WIN32)
#include <windows.h>

void *mmap(void *addr, unsigned long length, int prot, int flags, int fd,
           long offset) {
  (void)addr;
  (void)prot;
  (void)flags;
  (void)fd;
  (void)offset;
  return VirtualAlloc(NULL, length, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
}
#endif
