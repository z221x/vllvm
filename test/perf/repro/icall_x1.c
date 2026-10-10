#include <stdio.h>
typedef int Int32; typedef unsigned char UChar;
__attribute__((annotate("vllvm:icall")))
void BZ2_hbAssignCodes(Int32 *code, UChar *length, Int32 minLen, Int32 maxLen, Int32 alphaSize) {
  Int32 n, vec, i; vec = 0;
  for (n = minLen; n <= maxLen; n++) {
    for (i = 0; i < alphaSize; i++) if (length[i] == n) { code[i] = vec; vec++; }
    vec <<= 1;
  }
}
