/* zlib 性能负载：确定性数据生成 + compress2/uncompress 往返校验。
 * 驱动本身不参与混淆（模拟“应用代码调用被混淆库”的真实形态）。 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "zlib.h"

#ifndef IN_MB
#define IN_MB 32
#endif
#ifndef ROUNDS
#define ROUNDS 24
#endif

static unsigned lcg(unsigned *s) { return *s = *s * 1103515245u + 12345u; }

int main(void) {
  size_t in_sz = (size_t)IN_MB << 20;
  unsigned char *in = malloc(in_sz);
  unsigned char *comp = malloc(in_sz + 4096);
  unsigned char *back = malloc(in_sz);
  if (!in || !comp || !back)
    return 3;

  const char *txt = "The quick brown fox jumps over the lazy dog. ";
  unsigned st = 12345;
  for (size_t i = 0; i < in_sz;) {
    unsigned r = lcg(&st);
    if (r & 1) {
      size_t l = strlen(txt);
      size_t c = (r % 64) + 1;
      while (c-- && i < in_sz) {
        in[i++] = (unsigned char)txt[r % l];
        r++;
      }
    } else {
      size_t c = (r % 4096) + 1;
      while (c-- && i < in_sz)
        in[i++] = lcg(&st) & 0xff;
    }
  }

  unsigned long crc0 = (unsigned long)crc32(0L, in, (uInt)in_sz);
  unsigned long long total_c = 0, total_d = 0;

  for (int r = 0; r < ROUNDS; r++) {
    uLongf cap = compressBound((uLong)in_sz);
    if (compress2(comp, &cap, in, (uLong)in_sz, 6) != Z_OK)
      return 1;
    total_c += cap;
    uLongf blen = (uLong)in_sz;
    if (uncompress(back, &blen, comp, cap) != Z_OK)
      return 1;
    if (blen != in_sz ||
        crc0 != (unsigned long)crc32(0L, back, (uInt)blen))
      return 2;
    total_d += blen;
  }
  printf("OK %08lx %llu %llu\n", crc0, total_c, total_d);
  return 0;
}
