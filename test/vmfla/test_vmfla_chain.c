#include <stdio.h>

/* vmfla 链路目标：vllvm:vmfla 现在展开为 bb2func + merge + vmfla 三段。
 * 函数要有足够多的稠密块（bb2func 提取 >=2 个 helper 才会触发 merge），
 * 同时保留循环/分支给 vmfla 平坦化。 */
__attribute__((annotate("vllvm:vmfla")))
static unsigned chain_target(unsigned n) {
  unsigned a = n ^ 0x9e3779b9u;
  unsigned b = n * 0x85ebca6bu + 17u;
  a = (a * 3u) + (b >> 3) - (a << 1);
  b ^= (a << 5) | (b >> 27);
  for (int i = 0; i < 3; ++i) {
    a += 0x45d9f3bu * (unsigned)(i + 2);
    a = (a << 4) | (a >> 28);
    b = b * 5u + a;
  }
  if ((n & 1u) != 0u) {
    a = (a ^ b) * 7u + (b >> 2);
    a = a + (a >> 5) - (a >> 11);
  } else {
    b = (b - a) * 13u + (a >> 1);
    b = (b << 7) | (b >> 25);
  }
  switch (n % 3u) {
  case 0:
    a = a * 31u + b;
    b ^= a >> 3;
    a = a + b - (a << 2);
    break;
  case 1:
    b = (b ^ 0xc2b2ae35u) + a * 3u;
    a = (a >> 2) ^ (b << 3);
    b = b - a + (b >> 6);
    break;
  default:
    a = (a + b) * 17u;
    b = (b * 3u) ^ (a >> 4);
    a ^= b + (a << 5);
    break;
  }
  a = a * 3u + (b & 0xffffu);
  b = (b >> 7) ^ (a << 9);
  return a ^ (b * 0x45d9f3bu);
}

int main(void) {
  unsigned total = 0;
  for (unsigned i = 0; i < 10; ++i)
    total = total * 37u + chain_target(i * 13u + 1u);
  printf("vmfla-chain:%u\n", total);
  return 0;
}
