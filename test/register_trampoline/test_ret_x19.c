#include <stdio.h>

extern int my_call0(void);

__attribute__((noinline)) int target_value(void) { return 0x1234; }

int main(void) {
  int value = my_call0();
  printf("ret=%d\n", value);
  return value == 0x1234 ? 0 : 1;
}
