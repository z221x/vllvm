#include <stdio.h>

#if defined(VLLVM_TEST_VMFLA)
#define VLLVM_TEST_OBF __attribute__((annotate("vllvm:vmfla")))
#else
#define VLLVM_TEST_OBF
#endif

// 模拟解释器 dispatch：computed goto 产生 indirectbr + blockaddress
// 地址表。indirectbr 直连 case 块会绕过 vmfla 的 dispatcher，本用例
// 验证平坦化后状态机在旁路进入下仍输出一致。
VLLVM_TEST_OBF
static int interp(int seed) {
  static const void *const disptab[4] = {&&op_add, &&op_sub, &&op_xor,
                                         &&op_end};
  int acc = seed;
  int pc = 0;
  const void *target = disptab[0];

dispatch:
  goto *target;

op_add:
  acc += 3 + (pc & 1);
  if (++pc >= 24)
    goto op_end;
  if (pc & 4)
    goto op_sub;
  target = disptab[pc & 3];
  goto dispatch;

op_sub:
  acc -= 2 + (pc & 2);
  if (++pc >= 24)
    goto op_end;
  if (pc & 8)
    goto op_xor;
  target = disptab[pc & 3];
  goto dispatch;

op_xor:
  acc ^= 0x5f + (pc & 3);
  if (++pc >= 24)
    goto op_end;
  if (pc & 2)
    goto op_add;
  target = disptab[pc & 3];
  goto dispatch;

op_end:
  return acc;
}

int main(void) {
  printf("%d\n", (interp(7) ^ interp(-13)) & 1023);
  return 0;
}
