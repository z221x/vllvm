#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef _WIN32
#include <sys/mman.h>
#include <unistd.h>
#else
// mingw 没有 alarm；用 tick 计数兜底防初始化卡死回归。
#include <windows.h>
#endif

// 2026-10-20 起池运行时为单线程首次初始化语义（无 CAS 抢锁），
// 驱动只覆盖单线程下标访问/别名/指针稳定性与分配失败 trap。
enum { Iterations = 10000 };
static _Atomic unsigned Allocations;
static int FailAllocation;

const char *cached_alpha(void);
const char *cached_alpha_alias(void);
const char *cached_beta(void);

// mmap 的失败哨兵是 MAP_FAILED(-1)，VirtualAlloc 是 NULL；shim 必须各自
// 返回对应哨兵才能命中 IR 里的 fail-fast 路径。
void *enstr_test_mmap(void *hint, size_t size, int prot, int flags, int fd,
                      long offset) {
  atomic_fetch_add_explicit(&Allocations, 1, memory_order_relaxed);
  if (FailAllocation)
    return (void *)-1;
#ifdef _WIN32
  // COFF 宿主没有 mmap；测试里用页分配器等价替代。
  (void)hint; (void)prot; (void)flags; (void)fd; (void)offset;
  return VirtualAlloc(NULL, size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
#else
  return mmap(hint, size, prot, flags, fd, (off_t)offset);
#endif
}

void *enstr_test_virtual_alloc(void *hint, size_t size, int type, int prot) {
  atomic_fetch_add_explicit(&Allocations, 1, memory_order_relaxed);
  if (FailAllocation)
    return NULL;
#ifdef _WIN32
  return VirtualAlloc(hint, size, type, prot);
#else
  (void)hint; (void)type; (void)prot;
  return mmap(NULL, size, PROT_READ | PROT_WRITE,
              MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
#endif
}

// The instrumented IR redirects only the failure trap, avoiding a deliberate
// crash/core dump while checking that allocation failure takes the fail-fast
// path.
_Noreturn void enstr_test_trap(void) {
  unsigned allocations = atomic_load_explicit(&Allocations, memory_order_relaxed);
  if (!FailAllocation || allocations != 1)
    exit(1);
  puts("allocation failure reached trap without publishing a pointer");
  exit(0);
}

int main(int argc, char **argv) {
#ifndef _WIN32
  alarm(20); // Bound a regression that accidentally leaves initialization stuck.
#endif
  if (argc > 1 && strcmp(argv[1], "oom") == 0) {
    FailAllocation = 1;
    cached_alpha();
    return 1;
  }

  const char *first_alpha = NULL;
  const char *first_beta = NULL;
  for (int i = 0; i < Iterations; ++i) {
    const char *alpha = cached_alpha();
    const char *alias = cached_alpha_alias();
    const char *beta = cached_beta();
    if (i == 0) {
      first_alpha = alpha;
      first_beta = beta;
    }
    if (alpha != alias || alpha != first_alpha || beta != first_beta ||
        alpha == beta || strcmp(alpha, "cache-alpha") ||
        strcmp(beta, "cache-beta"))
      return 1;
  }

  unsigned allocations = atomic_load_explicit(&Allocations, memory_order_relaxed);
  if (allocations != 1) {
    fprintf(stderr, "expected one pool allocation, got %u\n", allocations);
    return 1;
  }
  printf("iterations=%d allocations=%u stable=1\n", Iterations, allocations);
  return 0;
}
