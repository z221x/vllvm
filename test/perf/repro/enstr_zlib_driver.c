#include <stdio.h>
#include <string.h>
#include "zlib.h"
static unsigned char in[1 << 20], comp[1 << 21], back[1 << 20];
int main(void) {
  const char *txt = "vllvm perf sample line %d\n";
  unsigned st = 999;
  size_t i = 0;
  while (i < sizeof in) {
    st = st * 1103515245u + 12345u;
    char buf[64];
    int n = snprintf(buf, sizeof buf, txt, (int)(st & 0xffff));
    for (int k = 0; k < n && i < sizeof in;)
      in[i++] = (unsigned char)buf[k++];
    for (int k = 0; k < 64 && i < sizeof in; k++)
      in[i++] = (unsigned char)(st >> 13), st = st * 1103515245u + 12345u;
  }
  uLongf clen = sizeof comp;
  int r = compress2(comp, &clen, in, sizeof in, 6);
  printf("compress2=%d clen=%lu\n", r, clen);
  if (r != Z_OK) return 1;
  uLongf blen = sizeof back;
  r = uncompress(back, &blen, comp, clen);
  printf("uncompress=%d\n", r);
  if (r != Z_OK) return 2;
  unsigned long c0 = crc32(0, in, sizeof in), c1 = crc32(0, back, blen);
  printf("crc %s\n", c0 == c1 ? "OK" : "MISMATCH");
  return c0 == c1 ? 0 : 3;
}
