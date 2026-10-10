/* vmp_kernels 驱动：KERNEL=1..5 选择内核，RUNS/ITER 由 -D 提供。 */
#include <stdint.h>

uint64_t k_leaf(uint64_t, uint64_t, const uint64_t *, uint64_t, uint64_t);
uint64_t k_loop(uint64_t, uint64_t);
uint64_t k_branch(uint64_t, uint64_t);
uint64_t k_mem(uint64_t *, uint64_t);
uint64_t k_call(uint64_t, uint64_t);

int main(void) {
  static uint64_t buf[256];
  for (int i = 0; i < 256; i++)
    buf[i] = (uint64_t)i * 0x9E3779B97F4A7C15ULL;
  uint64_t s = 0;
#if KERNEL == 1
  for (int r = 0; r < RUNS; r++)
    s += k_leaf((uint64_t)r, (uint64_t)r + 1, &buf[r & 255], (uint64_t)r + 2,
                (uint64_t)r + 3);
#elif KERNEL == 2
  for (int r = 0; r < RUNS; r++)
    s += k_loop(ITER, (uint64_t)r);
#elif KERNEL == 3
  for (int r = 0; r < RUNS; r++)
    s += k_branch((uint64_t)r, (uint64_t)r + 5);
#elif KERNEL == 4
  for (int r = 0; r < RUNS; r++)
    s += k_mem(buf, ITER);
#elif KERNEL == 5
  for (int r = 0; r < RUNS; r++)
    s += k_call(ITER, (uint64_t)r);
#endif
  return s == 0;
}
