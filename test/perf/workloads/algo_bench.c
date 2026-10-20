/* 常见算法（AES-128-CBC / MD5 / SHA-256）的 vmfla、bcf+vmfla 与组合
 * 函数级开销基准：OBF_MODE 选择标注（0=base 1=vmfla 2=bcf 3=bcf+vmfla
 * 4=combo2 5=combo 6=combo3，组合定义与 run_perfbench 的同名配置
 * 一致——组合性能只在算法级测，应用级组合仅做可用性），
 * 各算法标注其顶层处理函数。启动先校验官方测试向量（FIPS-197 /
 * RFC1321 / FIPS-180），再对 1MB 确定性缓冲做多轮处理，逐轮计时并
 * 校验轮间摘要一致；摘要同时打印供跨配置比对。 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifndef OBF_MODE
#define OBF_MODE 0
#endif

#if OBF_MODE == 1
#define OBF __attribute__((annotate("vllvm:vmfla")))
#elif OBF_MODE == 2
#define OBF __attribute__((annotate("vllvm:bcf")))
#elif OBF_MODE == 3
#define OBF __attribute__((annotate("vllvm:bcf,vmfla")))
#elif OBF_MODE == 4
#define OBF __attribute__((annotate("vllvm:enstr,icall,ibr")))
#elif OBF_MODE == 5
#define OBF __attribute__((annotate("vllvm:vmfla,ibr")))
#elif OBF_MODE == 6
#define OBF __attribute__((annotate("vllvm:enstr,icall,vmfla")))
#else
#define OBF
#endif

/* 计时输出的 volatile 汇，防止整个基准被优化掉。 */
static volatile uint32_t g_sink;

static double now_s(void) {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
}

/* ===================== AES-128（T 表实现） ===================== */

static const uint8_t SBOX[256] = {
    0x63, 0x7c, 0x77, 0x7b, 0xf2, 0x6b, 0x6f, 0xc5, 0x30, 0x01, 0x67, 0x2b,
    0xfe, 0xd7, 0xab, 0x76, 0xca, 0x82, 0xc9, 0x7d, 0xfa, 0x59, 0x47, 0xf0,
    0xad, 0xd4, 0xa2, 0xaf, 0x9c, 0xa4, 0x72, 0xc0, 0xb7, 0xfd, 0x93, 0x26,
    0x36, 0x3f, 0xf7, 0xcc, 0x34, 0xa5, 0xe5, 0xf1, 0x71, 0xd8, 0x31, 0x15,
    0x04, 0xc7, 0x23, 0xc3, 0x18, 0x96, 0x05, 0x9a, 0x07, 0x12, 0x80, 0xe2,
    0xeb, 0x27, 0xb2, 0x75, 0x09, 0x83, 0x2c, 0x1a, 0x1b, 0x6e, 0x5a, 0xa0,
    0x52, 0x3b, 0xd6, 0xb3, 0x29, 0xe3, 0x2f, 0x84, 0x53, 0xd1, 0x00, 0xed,
    0x20, 0xfc, 0xb1, 0x5b, 0x6a, 0xcb, 0xbe, 0x39, 0x4a, 0x4c, 0x58, 0xcf,
    0xd0, 0xef, 0xaa, 0xfb, 0x43, 0x4d, 0x33, 0x85, 0x45, 0xf9, 0x02, 0x7f,
    0x50, 0x3c, 0x9f, 0xa8, 0x51, 0xa3, 0x40, 0x8f, 0x92, 0x9d, 0x38, 0xf5,
    0xbc, 0xb6, 0xda, 0x21, 0x10, 0xff, 0xf3, 0xd2, 0xcd, 0x0c, 0x13, 0xec,
    0x5f, 0x97, 0x44, 0x17, 0xc4, 0xa7, 0x7e, 0x3d, 0x64, 0x5d, 0x19, 0x73,
    0x60, 0x81, 0x4f, 0xdc, 0x22, 0x2a, 0x90, 0x88, 0x46, 0xee, 0xb8, 0x14,
    0xde, 0x5e, 0x0b, 0xdb, 0xe0, 0x32, 0x3a, 0x0a, 0x49, 0x06, 0x24, 0x5c,
    0xc2, 0xd3, 0xac, 0x62, 0x91, 0x95, 0xe4, 0x79, 0xe7, 0xc8, 0x37, 0x6d,
    0x8d, 0xd5, 0x4e, 0xa9, 0x6c, 0x56, 0xf4, 0xea, 0x65, 0x7a, 0xae, 0x08,
    0xba, 0x78, 0x25, 0x2e, 0x1c, 0xa6, 0xb4, 0xc6, 0xe8, 0xdd, 0x74, 0x1f,
    0x4b, 0xbd, 0x8b, 0x8a, 0x70, 0x3e, 0xb5, 0x66, 0x48, 0x03, 0xf6, 0x0e,
    0x61, 0x35, 0x57, 0xb9, 0x86, 0xc1, 0x1d, 0x9e, 0xe1, 0xf8, 0x98, 0x11,
    0x69, 0xd9, 0x8e, 0x94, 0x9b, 0x1e, 0x87, 0xe9, 0xce, 0x55, 0x28, 0xdf,
    0x8c, 0xa1, 0x89, 0x0d, 0xbf, 0xe6, 0x42, 0x68, 0x41, 0x99, 0x2d, 0x0f,
    0xb0, 0x54, 0xbb, 0x16};

static uint32_t Te0[256], Te1[256], Te2[256], Te3[256];
static uint32_t aes_rk[44];
static volatile uint32_t g_key_word;

static uint8_t xt(uint8_t x) {
  return (uint8_t)((x << 1) ^ ((x & 0x80) ? 0x1b : 0));
}

static void aes_init_tables(void) {
  for (int i = 0; i < 256; i++) {
    uint8_t s = SBOX[i];
    uint32_t w = ((uint32_t)xt(s) << 24) | ((uint32_t)s << 16) |
                 ((uint32_t)s << 8) | (uint32_t)(s ^ xt(s));
    Te0[i] = w;
    Te1[i] = (w >> 8) | (w << 24);
    Te2[i] = (w >> 16) | (w << 16);
    Te3[i] = (w >> 24) | (w << 8);
  }
}

static uint32_t be32(const uint8_t *p) {
  return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
         ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

static void put32(uint8_t *p, uint32_t w) {
  p[0] = (uint8_t)(w >> 24);
  p[1] = (uint8_t)(w >> 16);
  p[2] = (uint8_t)(w >> 8);
  p[3] = (uint8_t)w;
}

static void aes128_expand(const uint8_t key[16]) {
  static const uint8_t RCON[10] = {0x01, 0x02, 0x04, 0x08, 0x10,
                                   0x20, 0x40, 0x80, 0x1b, 0x36};
  for (int i = 0; i < 4; i++)
    aes_rk[i] = be32(key + 4 * i);
  for (int i = 4; i < 44; i++) {
    uint32_t t = aes_rk[i - 1];
    if (i % 4 == 0) {
      t = (t << 8) | (t >> 24);
      t = ((uint32_t)SBOX[(t >> 24) & 0xff] << 24) |
          ((uint32_t)SBOX[(t >> 16) & 0xff] << 16) |
          ((uint32_t)SBOX[(t >> 8) & 0xff] << 8) |
          (uint32_t)SBOX[t & 0xff];
      t ^= (uint32_t)RCON[i / 4 - 1] << 24;
    }
    aes_rk[i] = aes_rk[i - 4] ^ t;
  }
}

/* 被标注的核心：CBC 加密整段缓冲（轮函数内联在 T 表轮循环里）。 */
OBF void aes128_cbc_encrypt(const uint8_t *in, uint8_t *out, size_t n,
                            const uint8_t iv[16]) {
  uint32_t p0 = be32(iv), p1 = be32(iv + 4), p2 = be32(iv + 8),
           p3 = be32(iv + 12);
  for (size_t off = 0; off < n; off += 16) {
    uint32_t s0 = be32(in + off) ^ p0 ^ aes_rk[0];
    uint32_t s1 = be32(in + off + 4) ^ p1 ^ aes_rk[1];
    uint32_t s2 = be32(in + off + 8) ^ p2 ^ aes_rk[2];
    uint32_t s3 = be32(in + off + 12) ^ p3 ^ aes_rk[3];
    uint32_t t0, t1, t2, t3;
    const uint32_t *rk = aes_rk + 4;
    for (int r = 0; r < 9; r++) {
      t0 = Te0[(s0 >> 24) & 0xff] ^ Te1[(s1 >> 16) & 0xff] ^
           Te2[(s2 >> 8) & 0xff] ^ Te3[s3 & 0xff] ^ rk[0];
      t1 = Te0[(s1 >> 24) & 0xff] ^ Te1[(s2 >> 16) & 0xff] ^
           Te2[(s3 >> 8) & 0xff] ^ Te3[s0 & 0xff] ^ rk[1];
      t2 = Te0[(s2 >> 24) & 0xff] ^ Te1[(s3 >> 16) & 0xff] ^
           Te2[(s0 >> 8) & 0xff] ^ Te3[s1 & 0xff] ^ rk[2];
      t3 = Te0[(s3 >> 24) & 0xff] ^ Te1[(s0 >> 16) & 0xff] ^
           Te2[(s1 >> 8) & 0xff] ^ Te3[s2 & 0xff] ^ rk[3];
      s0 = t0;
      s1 = t1;
      s2 = t2;
      s3 = t3;
      rk += 4;
    }
    t0 = ((uint32_t)SBOX[(s0 >> 24) & 0xff] << 24) |
         ((uint32_t)SBOX[(s1 >> 16) & 0xff] << 16) |
         ((uint32_t)SBOX[(s2 >> 8) & 0xff] << 8) |
         (uint32_t)SBOX[s3 & 0xff];
    t0 ^= aes_rk[40];
    t1 = ((uint32_t)SBOX[(s1 >> 24) & 0xff] << 24) |
         ((uint32_t)SBOX[(s2 >> 16) & 0xff] << 16) |
         ((uint32_t)SBOX[(s3 >> 8) & 0xff] << 8) |
         (uint32_t)SBOX[s0 & 0xff];
    t1 ^= aes_rk[41];
    t2 = ((uint32_t)SBOX[(s2 >> 24) & 0xff] << 24) |
         ((uint32_t)SBOX[(s3 >> 16) & 0xff] << 16) |
         ((uint32_t)SBOX[(s0 >> 8) & 0xff] << 8) |
         (uint32_t)SBOX[s1 & 0xff];
    t2 ^= aes_rk[42];
    t3 = ((uint32_t)SBOX[(s3 >> 24) & 0xff] << 24) |
         ((uint32_t)SBOX[(s0 >> 16) & 0xff] << 16) |
         ((uint32_t)SBOX[(s1 >> 8) & 0xff] << 8) |
         (uint32_t)SBOX[s2 & 0xff];
    t3 ^= aes_rk[43];
    put32(out + off, t0);
    put32(out + off + 4, t1);
    put32(out + off + 8, t2);
    put32(out + off + 12, t3);
    p0 = t0;
    p1 = t1;
    p2 = t2;
    p3 = t3;
  }
  g_key_word ^= aes_rk[43];
}

/* ===================== MD5 ===================== */

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

static uint64_t be64(const uint8_t *p) {
  return ((uint64_t)be32(p) << 32) | be32(p + 4);
}

static void put64(uint8_t *p, uint64_t w) {
  put32(p, (uint32_t)(w >> 32));
  put32(p + 4, (uint32_t)w);
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

/* ===================== SHA-256 ===================== */

static const uint32_t SHA_K[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1,
    0x923f82a4, 0xab1c5ed5, 0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3,
    0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786,
    0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147,
    0x06ca6351, 0x14292967, 0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13,
    0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85, 0xa2bfe8a1, 0xa81a664b,
    0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a,
    0x5b9cca4f, 0x682e6ff3, 0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
    0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};

static uint32_t ror(uint32_t x, int n) {
  return (x >> n) | (x << (32 - n));
}

/* 被标注的核心：整段缓冲 SHA-256（压缩函数内联 + padding）。 */
OBF void sha256_buf(const uint8_t *in, size_t n, uint8_t out[32]) {
  uint32_t h[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
                   0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
  size_t full = n / 64 * 64;
  for (size_t off = 0; off < full; off += 64) {
    uint32_t w[64];
    for (int i = 0; i < 16; i++)
      w[i] = be32(in + off + 4 * i);
    for (int i = 16; i < 64; i++)
      w[i] = (ror(w[i - 2], 17) ^ ror(w[i - 2], 19) ^ (w[i - 2] >> 10)) +
             w[i - 7] +
             (ror(w[i - 15], 7) ^ ror(w[i - 15], 18) ^ (w[i - 15] >> 3)) +
             w[i - 16];
    uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4], f = h[5],
             g = h[6], hh = h[7];
    for (int i = 0; i < 64; i++) {
      uint32_t t1 = hh + (ror(e, 6) ^ ror(e, 11) ^ ror(e, 25)) +
                    ((e & f) ^ (~e & g)) + SHA_K[i] + w[i];
      uint32_t t2 = (ror(a, 2) ^ ror(a, 13) ^ ror(a, 22)) +
                    ((a & b) ^ (a & c) ^ (b & c));
      hh = g;
      g = f;
      f = e;
      e = d + t1;
      d = c;
      c = b;
      b = a;
      a = t1 + t2;
    }
    h[0] += a;
    h[1] += b;
    h[2] += c;
    h[3] += d;
    h[4] += e;
    h[5] += f;
    h[6] += g;
    h[7] += hh;
  }
  uint8_t tail[128];
  size_t rem = n - full;
  memcpy(tail, in + full, rem);
  tail[rem] = 0x80;
  size_t padend = (rem + 8 < 64) ? 64 : 128;
  memset(tail + rem + 1, 0, padend - rem - 1);
  uint64_t bits = (uint64_t)n * 8;
  for (int i = 0; i < 8; i++)
    tail[padend - 8 + i] = (uint8_t)(bits >> (56 - 8 * i));
  for (size_t off = 0; off < padend; off += 64) {
    uint32_t w[64];
    for (int i = 0; i < 16; i++)
      w[i] = be32(tail + off + 4 * i);
    for (int i = 16; i < 64; i++)
      w[i] = (ror(w[i - 2], 17) ^ ror(w[i - 2], 19) ^ (w[i - 2] >> 10)) +
             w[i - 7] +
             (ror(w[i - 15], 7) ^ ror(w[i - 15], 18) ^ (w[i - 15] >> 3)) +
             w[i - 16];
    uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4], f = h[5],
             g = h[6], hh = h[7];
    for (int i = 0; i < 64; i++) {
      uint32_t t1 = hh + (ror(e, 6) ^ ror(e, 11) ^ ror(e, 25)) +
                    ((e & f) ^ (~e & g)) + SHA_K[i] + w[i];
      uint32_t t2 = (ror(a, 2) ^ ror(a, 13) ^ ror(a, 22)) +
                    ((a & b) ^ (a & c) ^ (b & c));
      hh = g;
      g = f;
      f = e;
      e = d + t1;
      d = c;
      c = b;
      b = a;
      a = t1 + t2;
    }
    h[0] += a;
    h[1] += b;
    h[2] += c;
    h[3] += d;
    h[4] += e;
    h[5] += f;
    h[6] += g;
    h[7] += hh;
  }
  for (int i = 0; i < 8; i++)
    put32(out + 4 * i, h[i]);
  g_sink ^= h[0];
}

/* ===================== DES（教科书位置换实现） ===================== */

static const int DES_IP[64] = {
    58, 50, 42, 34, 26, 18, 10, 2,  60, 52, 44, 36, 28, 20, 12, 4,
    62, 54, 46, 38, 30, 22, 14, 6,  64, 56, 48, 40, 32, 24, 16, 8,
    57, 49, 41, 33, 25, 17, 9,  1,  59, 51, 43, 35, 27, 19, 11, 3,
    61, 53, 45, 37, 29, 21, 13, 5,  63, 55, 47, 39, 31, 23, 15, 7};
static const int DES_FP[64] = {
    40, 8,  48, 16, 56, 24, 64, 32, 39, 7,  47, 15, 55, 23, 63, 31,
    38, 6,  46, 14, 54, 22, 62, 30, 37, 5,  45, 13, 53, 21, 61, 29,
    36, 4,  44, 12, 52, 20, 60, 28, 35, 3,  43, 11, 51, 19, 59, 27,
    34, 2,  42, 10, 50, 18, 58, 26, 33, 1,  41, 9,  49, 17, 57, 25};
static const int DES_E[48] = {
    32, 1,  2,  3,  4,  5,  4,  5,  6,  7,  8,  9,
    8,  9,  10, 11, 12, 13, 12, 13, 14, 15, 16, 17,
    16, 17, 18, 19, 20, 21, 20, 21, 22, 23, 24, 25,
    24, 25, 26, 27, 28, 29, 28, 29, 30, 31, 32, 1};
static const int DES_P[32] = {
    16, 7,  20, 21, 29, 12, 28, 17, 1,  15, 23, 26, 5,  18, 31, 10,
    2,  8,  24, 14, 32, 27, 3,  9,  19, 13, 30, 6,  22, 11, 4,  25};
static const int DES_PC1[56] = {
    57, 49, 41, 33, 25, 17, 9,  1,  58, 50, 42, 34, 26, 18,
    10, 2,  59, 51, 43, 35, 27, 19, 11, 3,  60, 52, 44, 36,
    63, 55, 47, 39, 31, 23, 15, 7,  62, 54, 46, 38, 30, 22,
    14, 6,  61, 53, 45, 37, 29, 21, 13, 5,  28, 20, 12, 4};
static const int DES_PC2[48] = {
    14, 17, 11, 24, 1,  5,  3,  28, 15, 6,  21, 10,
    23, 19, 12, 4,  26, 8,  16, 7,  27, 20, 13, 2,
    41, 52, 31, 37, 47, 55, 30, 40, 51, 45, 33, 48,
    44, 49, 39, 56, 34, 53, 46, 42, 50, 36, 29, 32};
static const int DES_SHIFTS[16] = {1, 1, 2, 2, 2, 2, 2, 2,
                                   1, 2, 2, 2, 2, 2, 2, 1};
static const uint8_t DES_S[8][64] = {
    {14, 4,  13, 1,  2,  15, 11, 8,  3,  10, 6,  12, 5,  9,  0,  7,
     0,  15, 7,  4,  14, 2,  13, 1,  10, 6,  12, 11, 9,  5,  3,  8,
     4,  1,  14, 8,  13, 6,  2,  11, 15, 12, 9,  7,  3,  10, 5,  0,
     15, 12, 8,  2,  4,  9,  1,  7,  5,  11, 3,  14, 10, 0,  6,  13},
    {15, 1,  8,  14, 6,  11, 3,  4,  9,  7,  2,  13, 12, 0,  5,  10,
     3,  13, 4,  7,  15, 2,  8,  14, 12, 0,  1,  10, 6,  9,  11, 5,
     0,  14, 7,  11, 10, 4,  13, 1,  5,  8,  12, 6,  9,  3,  2,  15,
     13, 8,  10, 1,  3,  15, 4,  2,  11, 6,  7,  12, 0,  5,  14, 9},
    {10, 0,  9,  14, 6,  3,  15, 5,  1,  13, 12, 7,  11, 4,  2,  8,
     13, 7,  0,  9,  3,  4,  6,  10, 2,  8,  5,  14, 12, 11, 15, 1,
     13, 6,  4,  9,  8,  15, 3,  0,  11, 1,  2,  12, 5,  10, 14, 7,
     1,  10, 13, 0,  6,  9,  8,  7,  4,  15, 14, 3,  11, 5,  2,  12},
    {7,  13, 14, 3,  0,  6,  9,  10, 1,  2,  8,  5,  11, 12, 4,  15,
     13, 8,  11, 5,  6,  15, 0,  3,  4,  7,  2,  12, 1,  10, 14, 9,
     10, 6,  9,  0,  12, 11, 7,  13, 15, 1,  3,  14, 5,  2,  8,  4,
     3,  15, 0,  6,  10, 1,  13, 8,  9,  4,  5,  11, 12, 7,  2,  14},
    {2,  12, 4,  1,  7,  10, 11, 6,  8,  5,  3,  15, 13, 0,  14, 9,
     14, 11, 2,  12, 4,  7,  13, 1,  5,  0,  15, 10, 3,  9,  8,  6,
     4,  2,  1,  11, 10, 13, 7,  8,  15, 9,  12, 5,  6,  3,  0,  14,
     11, 8,  12, 7,  1,  14, 2,  13, 6,  15, 0,  9,  10, 4,  5,  3},
    {12, 1,  10, 15, 9,  2,  6,  8,  0,  13, 3,  4,  14, 7,  5,  11,
     10, 15, 4,  2,  7,  12, 9,  5,  6,  1,  13, 14, 0,  11, 3,  8,
     9,  14, 15, 5,  2,  8,  12, 3,  7,  0,  4,  10, 1,  13, 11, 6,
     4,  3,  2,  12, 9,  5,  15, 10, 11, 14, 1,  7,  6,  0,  8,  13},
    {4,  11, 2,  14, 15, 0,  8,  13, 3,  12, 9,  7,  5,  10, 6,  1,
     13, 0,  11, 7,  4,  9,  1,  10, 14, 3,  5,  12, 2,  15, 8,  6,
     1,  4,  11, 13, 12, 3,  7,  14, 10, 15, 6,  8,  0,  5,  9,  2,
     6,  11, 13, 8,  1,  4,  10, 7,  9,  5,  0,  15, 14, 2,  3,  12},
    {13, 2,  8,  4,  6,  15, 11, 1,  10, 9,  3,  14, 5,  0,  12, 7,
     1,  15, 13, 8,  10, 3,  7,  4,  12, 5,  6,  11, 0,  14, 9,  2,
     7,  11, 4,  1,  9,  12, 14, 2,  0,  6,  10, 13, 15, 3,  5,  8,
     2,  1,  14, 7,  4,  10, 8,  13, 15, 12, 9,  0,  3,  5,  6,  11}};

static uint64_t des_perm(uint64_t in, const int *tbl, int n) {
  uint64_t out = 0;
  for (int i = 0; i < n; i++)
    out = (out << 1) | ((in >> (64 - tbl[i])) & 1);
  return out;
}

static uint64_t des_subkey(uint64_t k56, int round) {
  /* PC2 输入是 56 位串（1..56，位 1 为最高位）。 */
  uint64_t out = 0;
  for (int i = 0; i < 48; i++)
    out = (out << 1) | ((k56 >> (56 - DES_PC2[i])) & 1);
  (void)round;
  return out;
}

/* 被标注的核心：整段缓冲 DES-ECB 加密（含轮函数与 S 盒）。 */
OBF void des_ecb_encrypt_buf(const uint8_t *in, uint8_t *out, size_t n,
                             const uint8_t key[8]) {
  uint64_t k56 = des_perm(be64(key), DES_PC1, 56);
  uint64_t k56h = (k56 >> 28) & 0xfffffff, k56l = k56 & 0xfffffff;
  for (size_t off = 0; off < n; off += 8) {
    uint64_t ks[16];
    uint64_t c = k56h, d = k56l;
    for (int r = 0; r < 16; r++) {
      c = ((c << DES_SHIFTS[r]) | (c >> (28 - DES_SHIFTS[r]))) & 0xfffffff;
      d = ((d << DES_SHIFTS[r]) | (d >> (28 - DES_SHIFTS[r]))) & 0xfffffff;
      ks[r] = des_subkey((c << 28) | d, r);
    }
    uint64_t ip = des_perm(be64(in + off), DES_IP, 64);
    uint32_t l = (uint32_t)(ip >> 32), r = (uint32_t)ip;
    for (int rnd = 0; rnd < 16; rnd++) {
      uint64_t er = des_perm((uint64_t)r << 32, DES_E, 48) ^ ks[rnd];
      uint32_t sbox = 0;
      for (int b = 0; b < 8; b++) {
        uint64_t six = (er >> (42 - 6 * b)) & 0x3f;
        int row = (int)(((six >> 4) & 2) | (six & 1));
        int col = (int)((six >> 1) & 0xf);
        sbox = (sbox << 4) | DES_S[b][row * 16 + col];
      }
      uint32_t f = (uint32_t)des_perm((uint64_t)sbox << 32, DES_P, 32);
      uint32_t nl = r;
      r = l ^ f;
      l = nl;
    }
    uint64_t pre = ((uint64_t)r << 32) | l;
    put64(out + off, des_perm(pre, DES_FP, 64));
  }
  g_sink ^= (uint32_t)k56l;
}

/* ===================== SM4（GB/T 32907-2016） ===================== */

static const uint32_t SM4_FK[4] = {0xa3b1bac6, 0x56aa3350, 0x677d9197,
                                   0xb27022dc};
static uint32_t SM4_CK[32];
static const uint8_t SM4_SBOX[256] = {
    0xd6, 0x90, 0xe9, 0xfe, 0xcc, 0xe1, 0x3d, 0xb7, 0x16, 0xb6, 0x14,
    0xc2, 0x28, 0xfb, 0x2c, 0x05, 0x2b, 0x67, 0x9a, 0x76, 0x2a, 0xbe,
    0x04, 0xc3, 0xaa, 0x44, 0x13, 0x26, 0x49, 0x86, 0x06, 0x99, 0x9c,
    0x42, 0x50, 0xf4, 0x91, 0xef, 0x98, 0x7a, 0x33, 0x54, 0x0b, 0x43,
    0xed, 0xcf, 0xac, 0x62, 0xe4, 0xb3, 0x1c, 0xa9, 0xc9, 0x08, 0xe8,
    0x95, 0x80, 0xdf, 0x94, 0xfa, 0x75, 0x8f, 0x3f, 0xa6, 0x47, 0x07,
    0xa7, 0xfc, 0xf3, 0x73, 0x17, 0xba, 0x83, 0x59, 0x3c, 0x19, 0xe6,
    0x85, 0x4f, 0xa8, 0x68, 0x6b, 0x81, 0xb2, 0x71, 0x64, 0xda, 0x8b,
    0xf8, 0xeb, 0x0f, 0x4b, 0x70, 0x56, 0x9d, 0x35, 0x1e, 0x24, 0x0e,
    0x5e, 0x63, 0x58, 0xd1, 0xa2, 0x25, 0x22, 0x7c, 0x3b, 0x01, 0x21,
    0x78, 0x87, 0xd4, 0x00, 0x46, 0x57, 0x9f, 0xd3, 0x27, 0x52, 0x4c,
    0x36, 0x02, 0xe7, 0xa0, 0xc4, 0xc8, 0x9e, 0xea, 0xbf, 0x8a, 0xd2,
    0x40, 0xc7, 0x38, 0xb5, 0xa3, 0xf7, 0xf2, 0xce, 0xf9, 0x61, 0x15,
    0xa1, 0xe0, 0xae, 0x5d, 0xa4, 0x9b, 0x34, 0x1a, 0x55, 0xad, 0x93,
    0x32, 0x30, 0xf5, 0x8c, 0xb1, 0xe3, 0x1d, 0xf6, 0xe2, 0x2e, 0x82,
    0x66, 0xca, 0x60, 0xc0, 0x29, 0x23, 0xab, 0x0d, 0x53, 0x4e, 0x6f,
    0xd5, 0xdb, 0x37, 0x45, 0xde, 0xfd, 0x8e, 0x2f, 0x03, 0xff, 0x6a,
    0x72, 0x6d, 0x6c, 0x5b, 0x51, 0x8d, 0x1b, 0xaf, 0x92, 0xbb, 0xdd,
    0xbc, 0x7f, 0x11, 0xd9, 0x5c, 0x41, 0x1f, 0x10, 0x5a, 0xd8, 0x0a,
    0xc1, 0x31, 0x88, 0xa5, 0xcd, 0x7b, 0xbd, 0x2d, 0x74, 0xd0, 0x12,
    0xb8, 0xe5, 0xb4, 0xb0, 0x89, 0x69, 0x97, 0x4a, 0x0c, 0x96, 0x77,
    0x7e, 0x65, 0xb9, 0xf1, 0x09, 0xc5, 0x6e, 0xc6, 0x84, 0x18, 0xf0,
    0x7d, 0xec, 0x3a, 0xdc, 0x4d, 0x20, 0x79, 0xee, 0x5f, 0x3e, 0xd7,
    0xcb, 0x39, 0x48};

static void sm4_init_ck(void) {
  /* CK_i 的第 j 字节 = (28i + 7j) mod 256，避免整表硬编码。 */
  for (unsigned i = 0; i < 32; i++) {
    uint32_t v = 0;
    for (unsigned j = 0; j < 4; j++)
      v = (v << 8) | ((28 * i + 7 * j) & 0xff);
    SM4_CK[i] = v;
  }
}

static uint32_t sm4_rotl(uint32_t x, int n) {
  return (x << n) | (x >> (32 - n));
}

static uint32_t sm4_tau(uint32_t x) {
  return ((uint32_t)SM4_SBOX[(x >> 24) & 0xff] << 24) |
         ((uint32_t)SM4_SBOX[(x >> 16) & 0xff] << 16) |
         ((uint32_t)SM4_SBOX[(x >> 8) & 0xff] << 8) |
         (uint32_t)SM4_SBOX[x & 0xff];
}

/* 被标注的核心：整段缓冲 SM4-ECB 加密。 */
OBF void sm4_ecb_encrypt_buf(const uint8_t *in, uint8_t *out, size_t n,
                             const uint8_t key[16]) {
  uint32_t mk[4], k[36];
  for (int i = 0; i < 4; i++)
    mk[i] = be32(key + 4 * i);
  for (int i = 0; i < 4; i++)
    k[i] = mk[i] ^ SM4_FK[i];
  uint32_t rk[32];
  for (int i = 0; i < 32; i++) {
    uint32_t t = k[i + 1] ^ k[i + 2] ^ k[i + 3] ^ SM4_CK[i];
    uint32_t b = sm4_tau(t);
    uint32_t l = b ^ sm4_rotl(b, 13) ^ sm4_rotl(b, 23);
    rk[i] = k[i] ^ l;
    k[i + 4] = rk[i];
  }
  for (size_t off = 0; off < n; off += 16) {
    uint32_t x[36];
    for (int i = 0; i < 4; i++)
      x[i] = be32(in + off + 4 * i);
    for (int i = 0; i < 32; i++) {
      uint32_t t = x[i + 1] ^ x[i + 2] ^ x[i + 3] ^ rk[i];
      uint32_t b = sm4_tau(t);
      uint32_t l = b ^ sm4_rotl(b, 2) ^ sm4_rotl(b, 10) ^ sm4_rotl(b, 18) ^
                   sm4_rotl(b, 24);
      x[i + 4] = x[i] ^ l;
    }
    for (int i = 0; i < 4; i++)
      put32(out + off + 4 * i, x[35 - i]);
  }
  g_sink ^= rk[31];
}

/* ===================== RC4 ===================== */

/* 被标注的核心：整段缓冲 RC4 流加密切（KSA 内联）。 */
OBF void rc4_xor_buf(const uint8_t *in, uint8_t *out, size_t n,
                     const uint8_t *key, size_t keylen) {
  uint8_t s[256];
  for (int i = 0; i < 256; i++)
    s[i] = (uint8_t)i;
  uint8_t j = 0;
  for (int i = 0; i < 256; i++) {
    j = j + s[i] + key[i % keylen];
    uint8_t t = s[i];
    s[i] = s[j];
    s[j] = t;
  }
  uint8_t a = 0, b = 0;
  for (size_t off = 0; off < n; off++) {
    a = a + 1;
    b = b + s[a];
    uint8_t t = s[a];
    s[a] = s[b];
    s[b] = t;
    out[off] = in[off] ^ s[(uint8_t)(s[a] + s[b])];
  }
  g_sink ^= b;
}

/* ===================== CRC32（IEEE/zlib） ===================== */

static uint32_t CRC32_TAB[256];

static void crc32_init(void) {
  for (uint32_t i = 0; i < 256; i++) {
    uint32_t c = i;
    for (int k = 0; k < 8; k++)
      c = (c & 1) ? (0xedb88320u ^ (c >> 1)) : (c >> 1);
    CRC32_TAB[i] = c;
  }
}

/* 被标注的核心：整段缓冲 CRC32。 */
OBF uint32_t crc32_buf(const uint8_t *in, size_t n) {
  uint32_t c = 0xffffffffu;
  for (size_t off = 0; off < n; off++)
    c = CRC32_TAB[(c ^ in[off]) & 0xff] ^ (c >> 8);
  g_sink ^= c;
  return c ^ 0xffffffffu;
}

/* ===================== 校验与计时驱动 ===================== */

#define BUF_SZ (1 << 20)
static uint8_t g_buf[BUF_SZ];
static uint8_t g_out[BUF_SZ];
static volatile uint8_t g_key[16];

static void fill_buf(void) {
  uint64_t st = 88172645463325252ULL;
  for (size_t i = 0; i < BUF_SZ; i++) {
    st = st * 6364136223846793005ULL + 1442695040888963407ULL;
    g_buf[i] = (uint8_t)(st >> 33);
  }
  for (int i = 0; i < 16; i++)
    g_key[i] = (uint8_t)(0x10 + i * 7);
}

static void hex(const uint8_t *p, int n, char *dst) {
  static const char HC[] = "0123456789abcdef";
  for (int i = 0; i < n; i++) {
    dst[2 * i] = HC[p[i] >> 4];
    dst[2 * i + 1] = HC[p[i] & 15];
  }
  dst[2 * n] = 0;
}

static void expect(const char *what, const char *got, const char *want) {
  if (strcmp(got, want) != 0) {
    fprintf(stderr, "%s vector mismatch: got=%s want=%s\n", what, got, want);
    exit(1);
  }
}

static void bench(const char *name, int rounds, void (*one_round)(void),
                  void (*digest)(char *)) {
  char ref[80];
  one_round(); /* 预热 + 取参照摘要 */
  digest(ref);
  if (rounds > 256)
    rounds = 256;
  double ts[256];
  for (int r = 0; r < rounds; r++) {
    double t0 = now_s();
    one_round();
    ts[r] = now_s() - t0;
    char cur[80];
    digest(cur);
    if (strcmp(cur, ref) != 0) {
      fprintf(stderr, "%s digest drift at round %d\n", name, r);
      exit(1);
    }
  }
  printf("%s times=", name);
  for (int r = 0; r < rounds; r++)
    printf("%s%.6f", r ? "," : "", ts[r]);
  printf("\n%s digest=%s\n", name, ref);
}

static uint8_t aes_dig[16], md5_dig[16], sha_dig[32];
static uint8_t des_dig[8], sm4_dig[16], rc4_dig[16];
static uint32_t crc_dig;
static const uint8_t G_IV[16] = {0, 1, 2, 3, 4, 5, 6, 7,
                                 8, 9, 10, 11, 12, 13, 14, 15};
static const uint8_t DES_KEY[8] = {0x13, 0x34, 0x57, 0x79, 0x9b, 0xbc, 0xdf,
                                   0xf1};
static const uint8_t SM4_KEY[16] = {0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd,
                                    0xef, 0xfe, 0xdc, 0xba, 0x98, 0x76, 0x54,
                                    0x32, 0x10};
static const uint8_t RC4_KEY[3] = {'K', 'e', 'y'};

static void aes_round(void) {
  uint8_t key[16];
  for (int i = 0; i < 16; i++)
    key[i] = g_key[i];
  aes128_expand(key);
  aes128_cbc_encrypt(g_buf, g_out, BUF_SZ, G_IV);
  for (int i = 0; i < 16; i++)
    aes_dig[i] = g_out[BUF_SZ - 16 + i] ^ g_out[i];
}

static void md5_round(void) { md5_buf(g_buf, BUF_SZ, md5_dig); }
static void sha_round(void) { sha256_buf(g_buf, BUF_SZ, sha_dig); }
static void aes_digest(char *s) { hex(aes_dig, 16, s); }
static void md5_digest(char *s) { hex(md5_dig, 16, s); }
static void sha_digest(char *s) { hex(sha_dig, 32, s); }

static void des_round(void) {
  des_ecb_encrypt_buf(g_buf, g_out, BUF_SZ, DES_KEY);
  for (int i = 0; i < 8; i++)
    des_dig[i] = g_out[BUF_SZ - 8 + i] ^ g_out[i];
}
static void sm4_round(void) {
  sm4_ecb_encrypt_buf(g_buf, g_out, BUF_SZ, SM4_KEY);
  for (int i = 0; i < 16; i++)
    sm4_dig[i] = g_out[BUF_SZ - 16 + i] ^ g_out[i];
}
static void rc4_round(void) {
  rc4_xor_buf(g_buf, g_out, BUF_SZ, RC4_KEY, sizeof RC4_KEY);
  for (int i = 0; i < 16; i++)
    rc4_dig[i] = g_out[i] ^ g_out[BUF_SZ - 16 + i];
}
static void crc_round(void) { crc_dig = crc32_buf(g_buf, BUF_SZ); }
static void des_digest(char *s) { hex(des_dig, 8, s); }
static void sm4_digest(char *s) { hex(sm4_dig, 16, s); }
static void rc4_digest(char *s) { hex(rc4_dig, 16, s); }
static void crc_digest(char *s) { sprintf(s, "%08x", crc_dig); }

int main(void) {
  aes_init_tables();
  sm4_init_ck();
  crc32_init();
  fill_buf();

  /* FIPS-197 AES-128 单块向量（CBC iv=0 时首块密文等于 ECB）。 */
  {
    uint8_t key[16], out[16], iv[16] = {0};
    for (int i = 0; i < 16; i++)
      key[i] = (uint8_t)i;
    static const uint8_t FIPS_PT[16] = {0x00, 0x11, 0x22, 0x33, 0x44, 0x55,
                                        0x66, 0x77, 0x88, 0x99, 0xaa, 0xbb,
                                        0xcc, 0xdd, 0xee, 0xff};
    static const uint8_t FIPS_CT[16] = {0x69, 0xc4, 0xe0, 0xd8, 0x6a, 0x7b,
                                        0x04, 0x30, 0xd8, 0xcd, 0xb7, 0x80,
                                        0x70, 0xb4, 0xc5, 0x5a};
    aes128_expand(key);
    aes128_cbc_encrypt(FIPS_PT, out, 16, iv);
    char got[33], want[33];
    hex(out, 16, got);
    hex(FIPS_CT, 16, want);
    expect("aes", got, want);
  }
  /* RFC1321 MD5("abc")；FIPS-180 SHA-256("abc")。 */
  {
    static const uint8_t ABC[3] = {'a', 'b', 'c'};
    char got[80];
    uint8_t d[32];
    md5_buf(ABC, 3, d);
    hex(d, 16, got);
    expect("md5", got,
           "900150983cd24fb0d6963f7d28e17f72");
    sha256_buf(ABC, 3, d);
    hex(d, 32, got);
    expect("sha256", got,
           "ba7816bf8f01cfea414140de5dae2223"
           "b00361a396177a9cb410ff61f20015ad");
  }
  /* FIPS-46 DES 单块向量：key=133457799BBCDFF1，pt=0123456789ABCDEF。 */
  {
    static const uint8_t PT[8] = {0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd,
                                  0xef};
    uint8_t out[8];
    des_ecb_encrypt_buf(PT, out, 8, DES_KEY);
    char got[32];
    hex(out, 8, got);
    expect("des", got, "85e813540f0ab405");
  }
  /* GB/T 32907-2016 SM4 单块向量：明文=密钥。 */
  {
    uint8_t out[16];
    sm4_ecb_encrypt_buf(SM4_KEY, out, 16, SM4_KEY);
    char got[40];
    hex(out, 16, got);
    expect("sm4", got, "681edf34d206965e86b3e94f536e4246");
  }
  /* RC4 官方向量：key="Key"，plaintext="Plaintext"。 */
  {
    static const uint8_t PT[9] = {'P', 'l', 'a', 'i', 'n', 't', 'e', 'x',
                                  't'};
    uint8_t out[9];
    rc4_xor_buf(PT, out, 9, RC4_KEY, sizeof RC4_KEY);
    char got[32];
    hex(out, 9, got);
    expect("rc4", got, "bbf316e8d940af0ad3");
  }
  /* CRC32("123456789") = CBF43926（IEEE/zlib）。 */
  {
    static const uint8_t T[9] = {'1', '2', '3', '4', '5', '6', '7', '8',
                                 '9'};
    char got[16];
    sprintf(got, "%08x", crc32_buf(T, 9));
    expect("crc32", got, "cbf43926");
  }

  bench("aes", 150, aes_round, aes_digest);
  bench("md5", 60, md5_round, md5_digest);
  bench("sha256", 60, sha_round, sha_digest);
  bench("des", 60, des_round, des_digest);
  bench("sm4", 60, sm4_round, sm4_digest);
  bench("rc4", 150, rc4_round, rc4_digest);
  bench("crc32", 150, crc_round, crc_digest);
  return (int)(g_sink & 0);
}
