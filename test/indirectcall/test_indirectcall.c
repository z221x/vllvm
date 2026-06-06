#include "stdio.h"
void funcA() { printf("This is funcA\n"); }
void funcB() { printf("This is funcB\n"); }
void funcC() { printf("This is funcC\n"); }
void funcD() { printf("This is funcD\n"); }
int funcE() { return 1; };
void funcF() { printf("This is funcF\n"); }
int main() {
  funcA();
  funcB();
  funcC();
  funcD();
  if (funcE()) {
    funcF();
  } else {
    printf("This is funcmain\n");
  }
}