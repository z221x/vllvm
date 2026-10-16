#include <stdio.h>

#if defined(VLLVM_TEST_OBF)
#define VLLVM_TEST_OBF_MARK __attribute__((annotate("vllvm:vmfla,bcf")))
#else
#define VLLVM_TEST_OBF_MARK
#endif

VLLVM_TEST_OBF_MARK
static int mix(int seed) {
  int acc = seed;
  for (int i = 0; i < 32; ++i)
    acc = acc * 1103515245 + 12345, acc ^= acc >> 7;
  return acc & 32767;
}

int main(void) {
  printf("%d\n", mix(9));
  return 0;
}
