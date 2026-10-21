/* MD5（RFC1321）单算法基准：OBF_MODE 选择标注配置，标注顶层 md5_buf；
 * 启动先校验 MD5("abc")，再对 1MB 确定性缓冲做多轮压缩，逐轮计时并
 * 校验轮间摘要一致。 */
#include "algo_common.h"

static const uint32_t MD5_K[64] = {
    0xd76aa478, 0xe8c7b756, 0x242070db, 0xc1bdceee, 0xf57c0faf, 0x4787c62a,
    0xa8304613, 0xfd469501, 0x698098d8, 0x8b44f7af, 0xffff5bb1, 0x895cd7be,
    0x6b901122, 0xfd987193, 0xa679438e, 0x49b40821, 0xf61e2562, 0xc040b340,
    0x265e5a51, 0xe9b6c7aa, 0xd62f105d, 0x02441453, 0xd8a1e681, 0xe7d3fbc8,
    0x21e1cde6, 0xc33707d6, 0xf4d50d87, 0x455a14ed, 0xa9e3e905, 0xfcefa3f8,
    0x676f02d9, 0x8d2a4c8a, 0xfffa3942, 0x8771f681, 0x6d9d6122, 0xfde5380c,
    0xa4beea44, 0x4bdecfa9, 0xf6bb4b60, 0xbebfbc70, 0x289b7ec6, 0xeaa127fa,
    0xd4ef3085, 0x04881d05, 0xd9d4d039, 0xe6db99e5, 0x1fa27cf8, 0xc4ac5665,
    0xf4292244, 0x432aff97, 0xab9423a7, 0xfc93a039, 0x655b59c3, 0x8f0ccc92,
    0xffeff47d, 0x85845dd1, 0x6fa87e4f, 0xfe2ce6e0, 0xa3014314, 0x4e0811a1,
    0xf7537e82, 0xbd3af235, 0x2ad7d2bb, 0xeb86d391};
static const int MD5_R[64] = {7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22,
                              7, 12, 17, 22, 5, 9,  14, 20, 5, 9,  14, 20,
                              5, 9,  14, 20, 5, 9,  14, 20, 4, 11, 16, 23,
                              4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23,
                              6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21,
                              6, 10, 15, 21};

static uint32_t rol(uint32_t x, int n) {
  return (x << n) | (x >> (32 - n));
}

/* 被标注的核心：整段缓冲 MD5（压缩函数内联 + padding）。 */
OBF void md5_buf(const uint8_t *in, size_t n, uint8_t out[16]) {
  uint32_t a0 = 0x67452301, b0 = 0xefcdab89, c0 = 0x98badcfe,
           d0 = 0x10325476;
  size_t full = n / 64 * 64;
  for (size_t off = 0; off < full; off += 64) {
    uint32_t m[16];
    for (int i = 0; i < 16; i++)
      m[i] = (uint32_t)in[off + 4 * i] | ((uint32_t)in[off + 4 * i + 1] << 8) |
             ((uint32_t)in[off + 4 * i + 2] << 16) |
             ((uint32_t)in[off + 4 * i + 3] << 24);
    uint32_t a = a0, b = b0, c = c0, d = d0;
    for (int i = 0; i < 64; i++) {
      uint32_t f;
      int g;
      if (i < 16) {
        f = (b & c) | (~b & d);
        g = i;
      } else if (i < 32) {
        f = (d & b) | (~d & c);
        g = (5 * i + 1) % 16;
      } else if (i < 48) {
        f = b ^ c ^ d;
        g = (3 * i + 5) % 16;
      } else {
        f = c ^ (b | ~d);
        g = (7 * i) % 16;
      }
      uint32_t tmp = d;
      d = c;
      c = b;
      b = b + rol(a + f + MD5_K[i] + m[g], MD5_R[i]);
      a = tmp;
    }
    a0 += a;
    b0 += b;
    c0 += c;
    d0 += d;
  }
  /* 尾块 + padding（含 64 位小端位长）。基准缓冲 n 是 64 的倍数，
   * 恒走"独立 padding 块"分支。 */
  uint8_t tail[128];
  size_t rem = n - full;
  memcpy(tail, in + full, rem);
  tail[rem] = 0x80;
  size_t padend = (rem + 8 < 64) ? 64 : 128;
  memset(tail + rem + 1, 0, padend - rem - 1);
  uint64_t bits = (uint64_t)n * 8;
  for (int i = 0; i < 8; i++)
    tail[padend - 8 + i] = (uint8_t)(bits >> (8 * i));
  for (size_t off = 0; off < padend; off += 64) {
    uint32_t m[16];
    for (int i = 0; i < 16; i++)
      m[i] = (uint32_t)tail[off + 4 * i] |
             ((uint32_t)tail[off + 4 * i + 1] << 8) |
             ((uint32_t)tail[off + 4 * i + 2] << 16) |
             ((uint32_t)tail[off + 4 * i + 3] << 24);
    uint32_t a = a0, b = b0, c = c0, d = d0;
    for (int i = 0; i < 64; i++) {
      uint32_t f;
      int g;
      if (i < 16) {
        f = (b & c) | (~b & d);
        g = i;
      } else if (i < 32) {
        f = (d & b) | (~d & c);
        g = (5 * i + 1) % 16;
      } else if (i < 48) {
        f = b ^ c ^ d;
        g = (3 * i + 5) % 16;
      } else {
        f = c ^ (b | ~d);
        g = (7 * i) % 16;
      }
      uint32_t tmp = d;
      d = c;
      c = b;
      b = b + rol(a + f + MD5_K[i] + m[g], MD5_R[i]);
      a = tmp;
    }
    a0 += a;
    b0 += b;
    c0 += c;
    d0 += d;
  }
  for (int i = 0; i < 4; i++) {
    out[i] = (uint8_t)(a0 >> (8 * i));
    out[4 + i] = (uint8_t)(b0 >> (8 * i));
    out[8 + i] = (uint8_t)(c0 >> (8 * i));
    out[12 + i] = (uint8_t)(d0 >> (8 * i));
  }
  g_sink ^= a0;
}

static uint8_t md5_dig[16];

static void md5_round(void) { md5_buf(g_buf, BUF_SZ, md5_dig); }
static void md5_digest(char *s) { hex(md5_dig, 16, s); }

int main(void) {
  fill_buf();
  {
    static const uint8_t ABC[3] = {'a', 'b', 'c'};
    char got[80];
    uint8_t d[32];
    md5_buf(ABC, 3, d);
    hex(d, 16, got);
    expect("md5", got, "900150983cd24fb0d6963f7d28e17f72");
  }
  bench("md5", 60, md5_round, md5_digest);
  return (int)(g_sink & 0);
}
