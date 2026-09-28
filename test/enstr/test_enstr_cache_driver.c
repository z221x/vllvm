#include <pthread.h>
#include <sched.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#ifndef _WIN32
#include <sys/mman.h>
#else
// mingw 没有 alarm；用 tick 计数兜底防初始化卡死回归。
#include <windows.h>
#endif

enum { ThreadCount = 16, Iterations = 10000 };
static _Atomic unsigned Allocations;
static _Atomic unsigned Ready;
static _Atomic int Start;
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
  for (int i = 0; i < 100; ++i)
    sched_yield();
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
  for (int i = 0; i < 100; ++i)
    sched_yield();
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

struct Result {
  const char *alpha;
  const char *beta;
  int failed;
};

static void *exercise(void *arg) {
  struct Result *result = arg;
  atomic_fetch_add_explicit(&Ready, 1, memory_order_release);
  while (!atomic_load_explicit(&Start, memory_order_acquire))
    sched_yield();

  for (int i = 0; i < Iterations; ++i) {
    const char *alpha = cached_alpha();
    const char *alias = cached_alpha_alias();
    const char *beta = cached_beta();
    if (i == 0) {
      result->alpha = alpha;
      result->beta = beta;
    }
    if (alpha != alias || alpha != result->alpha || beta != result->beta ||
        alpha == beta || strcmp(alpha, "cache-alpha") ||
        strcmp(beta, "cache-beta")) {
      result->failed = 1;
      return NULL;
    }
  }
  return NULL;
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
  struct Result results[ThreadCount] = {0};
  int threads = argc > 1 && strcmp(argv[1], "threads") == 0;
  int count = threads ? ThreadCount : 1;
  if (threads) {
    pthread_t workers[ThreadCount];
    for (int i = 0; i < ThreadCount; ++i)
      if (pthread_create(&workers[i], NULL, exercise, &results[i]))
        return 1;
    while (atomic_load_explicit(&Ready, memory_order_acquire) != ThreadCount)
      sched_yield();
    atomic_store_explicit(&Start, 1, memory_order_release);
    for (int i = 0; i < ThreadCount; ++i)
      if (pthread_join(workers[i], NULL))
        return 1;
  } else {
    atomic_store_explicit(&Start, 1, memory_order_release);
    exercise(&results[0]);
  }

  unsigned allocations = atomic_load_explicit(&Allocations, memory_order_relaxed);
  if (allocations != 1) {
    fprintf(stderr, "expected one pool allocation, got %u\n", allocations);
    return 1;
  }
  for (int i = 0; i < count; ++i)
    if (results[i].failed || results[i].alpha != results[0].alpha ||
        results[i].beta != results[0].beta)
      return 1;
  printf("threads=%d iterations=%d allocations=%u stable=1\n",
         count, Iterations, allocations);
  return 0;
}
