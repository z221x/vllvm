#include <stdio.h>
typedef int Int32; typedef unsigned char UChar;
void BZ2_hbAssignCodes(Int32 *code, UChar *length, Int32 minLen, Int32 maxLen, Int32 alphaSize);
static void refCodes(Int32 *code, UChar *length, Int32 minLen, Int32 maxLen, Int32 alphaSize) {
  Int32 n, vec, i; vec = 0;
  for (n = minLen; n <= maxLen; n++) {
    for (i = 0; i < alphaSize; i++) if (length[i] == n) { code[i] = vec; vec++; }
    vec <<= 1;
  }
}
int main(void) {
  enum { N = 23 }; UChar length[N]; Int32 a[N], b[N];
  for (int i = 0; i < N; i++) length[i] = (UChar)(1 + (i * 7 + 3) % 9);
  BZ2_hbAssignCodes(a, length, 1, 9, N); refCodes(b, length, 1, 9, N);
  long sA=0,sB=0; for (int i=0;i<N;i++){sA+=a[i];sB+=b[i];}
  printf("%s icall=%ld ref=%ld\n", sA==sB?"OK":"MISMATCH", sA, sB);
  return sA!=sB;
}
