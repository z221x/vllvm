#if defined(VLLVM_TEST_ICALL)
#define VLLVM_ICALL __attribute__((annotate("vllvm:icall")))
#else
#define VLLVM_ICALL
#endif

// 外部链接目标：跨 TU 调用点对 module pass 不可见、无法改写，因此只入池
// 不做参数加密（入口保持明文 ABI，其他编译单元可以继续直调）。
VLLVM_ICALL __attribute__((noinline)) long cross_tu_target(long a, long b) {
  return a * 3 + b - 7;
}

// 内部链接目标：所有调用点都在本模块内可见、都会被改写，参数加密安全。
VLLVM_ICALL __attribute__((noinline)) static long internal_target(long a,
                                                                  long b) {
  return (a ^ 0x5f) + b * 11;
}

long cross_tu_entry(long seed) {
  return cross_tu_target(seed, seed + 1) + internal_target(seed, 2);
}
