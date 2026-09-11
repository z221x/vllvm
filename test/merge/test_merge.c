#include <stdio.h>

/* merge 目标函数：两个静态标记函数进入共享组，融进同一个 keyed
 * dispatcher；成员体内联后原函数应被删除。 */
__attribute__((annotate("vllvm:merge")))
static int merge_a(int x) {
  int r = x * 3 + 7;
  r ^= (r << 2) | (r >> 30);
  return r + x;
}

__attribute__((annotate("vllvm:merge")))
static int merge_b(int x) {
  int r = (x ^ 13) - 5;
  r = r * 9 - (x >> 1);
  r += (r ^ x) << 1;
  return r;
}

int main(void) {
  int acc = 0;
  for (int i = 0; i < 6; ++i) {
    acc += merge_a(i * 11 - 7);
    acc ^= merge_b(i * 5 + 2);
  }
  printf("merge:%d\n", acc);
  return 0;
}
