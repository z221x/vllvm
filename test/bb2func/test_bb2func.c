#include <stdio.h>

/* bb2func 目标函数：足够多的稠密块和线性链，保证确定性候选收集
 * （>=5 条语义指令的块）一定产生提取结果。 */
__attribute__((annotate("vllvm:bb2func")))
static unsigned bb2_target(unsigned n) {
  unsigned acc = n ^ 0x5bf03635u;
  acc = (acc * 3u) + (acc >> 2) - 7u;
  acc ^= (acc << 5) | (acc >> 27);
  for (int i = 0; i < 4; ++i) {
    acc += 0x45d9f3bu * (unsigned)(i + 1);
    acc = (acc << 3) | (acc >> 29);
  }
  switch (n & 3u) {
  case 0:
    acc = (acc ^ 0x9e3779b9u) + n;
    acc = acc * 7u + (acc % 11u);
    break;
  case 1:
    acc = (acc + 0x85ebca6bu) ^ (n * 13u);
    acc = (acc >> 1) ^ (acc << 31);
    break;
  case 2:
    acc = (acc - n) * 0x45d9f3bu;
    acc = acc + (acc >> 7) - (acc >> 17);
    break;
  default:
    acc = (acc * 31u) ^ (n + 0xc2b2ae35u);
    acc = (acc + (acc << 3)) ^ (acc >> 4);
    break;
  }
  acc = acc * 5u + 1u;
  acc ^= acc >> 16;
  acc = acc * 3u + (n & 0xffu);
  acc ^= acc >> 8;
  return acc;
}

int main(void) {
  unsigned total = 0;
  for (unsigned i = 0; i < 8; ++i)
    total = total * 31u + bb2_target(i * 7u + 3u);
  printf("bb2func:%u\n", total);
  return 0;
}
