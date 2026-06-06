#include "stdio.h"
int main() {
  int a1 = 0;
  printf("first\n");
  a1 = 2;
  if (a1) {
    printf("second\n");
  }
  while (1) {
    switch (a1) {
      case 2:
        printf("third\n");
        a1 = 3;
        break;
      case 3:
        printf("fourth\n");
        goto next;
      default:
        break;
    }
  }
next:
  try {
    printf("fifth\n");
    throw("sixth");
  } catch (const char* msg) {
    printf("%s\n", msg);
  }
  return 0;
}