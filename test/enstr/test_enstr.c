#include <stdint.h>
#include <stdio.h>
#include <string.h>
#if defined(VLLVM_TEST_ENSTR)
// Keep the native baseline unchanged; request encryption for every string user.
#pragma clang attribute push(__attribute__((annotate("vllvm:enstr"))), apply_to=function)
#endif
char* a1 = "This is func2";
char* a2 = "This is func3";
const char* func1() { return "This is func1"; }
void func2() { printf("%s\n", a1); }
void func3() {
  char** ptr = &a2;
  printf("%s\n", *ptr);
}
int main() {
  printf("%s\n", func1());
  func2();
  func3();
  printf("%x\n", (unsigned)0 - (unsigned)(-1956577150));
}
#if defined(VLLVM_TEST_ENSTR)
#pragma clang attribute pop
#endif
