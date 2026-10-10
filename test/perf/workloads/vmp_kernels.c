/* VMP 准入形态内核：按 test/vmp 夹具的可虚拟化形状构造，用于刻画
 * 解释器的每指令/每 hostcall 开销。VMP 宏由 -DKERNELS_VMP 开启。 */
#include <stdint.h>

#if defined(KERNELS_VMP)
#define VMP __attribute__((annotate("vllvm:vmp")))
#else
#define VMP
#endif

#define NOINLINE __attribute__((noinline))

/* 形态 1：无调用叶子表达式（算术 + 一次 load） */
VMP NOINLINE uint64_t k_leaf(uint64_t a, uint64_t b, const uint64_t *p,
                             uint64_t d, uint64_t e) {
  return a + b * 3u + *p * 5u + d * 7u + e * 11u;
}

/* 形态 2：紧凑循环（整个循环体虚拟化，每迭代约 4 条虚拟指令） */
VMP NOINLINE uint64_t k_loop(uint64_t n, uint64_t seed) {
  uint64_t x = seed;
  for (uint64_t i = 0; i < n; i++)
    x = x * 6364136223846793005ULL + 1543293;
  return x;
}

/* 形态 3：分支密集 */
VMP NOINLINE uint64_t k_branch(uint64_t a, uint64_t b) {
  uint64_t r = 0;
  if (a & 1)
    r += b * 3;
  else
    r -= a;
  if (b & 2)
    r ^= a << 3;
  else
    r += b >> 1;
  switch (a & 3) {
  case 0:
    r += 1;
    break;
  case 1:
    r += 2;
    break;
  case 2:
    r += 3;
    break;
  default:
    r += 4;
    break;
  }
  return r;
}

/* 形态 4：访存循环（读写指针参数） */
VMP NOINLINE uint64_t k_mem(uint64_t *p, uint64_t n) {
  uint64_t s = 0;
  for (uint64_t i = 0; i < n; i++)
    s += p[i] = p[i] + 1;
  return s;
}

/* 形态 5：hostcall（虚拟代码调用原生辅助，测往返成本） */
static uint64_t native_helper(uint64_t x) { return x * 2654435761u + 1; }
VMP NOINLINE uint64_t k_call(uint64_t n, uint64_t seed) {
  uint64_t x = seed;
  for (uint64_t i = 0; i < n; i++)
    x = native_helper(x);
  return x;
}
