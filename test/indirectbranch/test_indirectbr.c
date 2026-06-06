#include "stdio.h"
int funcE() { return 1; };
void funcF() { printf("This is funcF\n"); }
int main() {
  printf("BB1\n");
  goto BB2;
BB2:
  printf("BB2\n");
  goto BB3;
BB3:
  printf("BB3\n");
  goto BB4;
BB4:
  printf("BB4\n");
  goto BB5;
BB5:
  if (funcE()) {
    funcF();
  } else {
    printf("This is funcM\n");
  }
  switch (funcE()) {
  case 1:
    printf("This is func1\n");
    break;
  case 2:
    printf("This is func2\n");
    break;
  case 3:
    printf("This is func3\n");
    break;
  case 4:
    printf("This is func4\n");
    break;
  }
}