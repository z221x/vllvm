/* flower 花指令回归 fixture：mix/accumulate 标注 vllvm:flower，
 * 主流程输出校验和并以低位作退出码，混淆前后必须一致。
 * VLLVM_TEST_FLOWER_COMBO 用于 flower+bcf 同函数组合用例。 */
#include <stdio.h>

#if defined(VLLVM_TEST_FLOWER_COMBO)
#define VLLVM_FLOWER __attribute__((annotate("vllvm:flower,bcf")))
#elif defined(VLLVM_TEST_FLOWER)
#define VLLVM_FLOWER __attribute__((annotate("vllvm:flower")))
#else
#define VLLVM_FLOWER
#endif

VLLVM_FLOWER static unsigned mix(unsigned x) {
  x ^= 0x9e3779b9U;
  x = (x << 13) | (x >> 19);
  x += 0x85ebca6bU;
  x = (x << 7) | (x >> 25);
  x ^= (x >> 16);
  return x;
}

VLLVM_FLOWER static unsigned accumulate(const unsigned *v, unsigned n) {
  unsigned acc = 2166136261U;
  for (unsigned i = 0; i < n; ++i)
    acc = mix(acc ^ (v[i] + i * 31U));
  return acc;
}

int main(void) {
  unsigned v[8] = {3, 1, 4, 1, 5, 9, 2, 6};
  unsigned chk = mix(accumulate(v, 8));
  printf("%u\n", chk);
  return (int)(chk & 0x3fU);
}
