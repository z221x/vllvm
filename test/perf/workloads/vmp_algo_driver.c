/* vmp_algos 驱动：ALGO=1..10 选择，RUNS 由 -D 提供。启动时只校验当前
 * 算法的官方测试向量（失败即非零退出）；哈希按 64 字节块（压缩函数
 * 原语）计，消息字经全局标量传入、缓冲区组包在原生驱动侧完成。 */
#include <stdint.h>
#include <string.h>

uint64_t fib_recursive(uint64_t);
uint64_t fib_iterative(uint64_t);
uint32_t crc32_word(uint32_t, uint64_t);
void sha256_block(void);
void sha1_block(void);
void md5_block(void);

extern uint32_t sha_h0, sha_h1, sha_h2, sha_h3, sha_h4, sha_h5, sha_h6,
    sha_h7;
extern uint32_t s1_a, s1_b, s1_c, s1_d, s1_e;
extern uint32_t m5_a, m5_b, m5_c, m5_d;
extern uint32_t shaw0, shaw1, shaw2, shaw3, shaw4, shaw5, shaw6,
    shaw7, shaw8, shaw9, shaw10, shaw11, shaw12, shaw13, shaw14,
    shaw15;
extern uint32_t s1w0, s1w1, s1w2, s1w3, s1w4, s1w5, s1w6, s1w7, s1w8, s1w9,
    s1w10, s1w11, s1w12, s1w13, s1w14, s1w15;
extern uint32_t m5w0, m5w1, m5w2, m5w3, m5w4, m5w5, m5w6, m5w7, m5w8, m5w9,
    m5w10, m5w11, m5w12, m5w13, m5w14, m5w15;

static uint8_t buf4k[4096];

static void fill_buf(void) {
  uint64_t st = 88172645463325252ULL;
  for (unsigned i = 0; i < sizeof buf4k; i++) {
    st = st * 6364136223846793005ULL + 1442695040888963407ULL;
    buf4k[i] = (uint8_t)(st >> 33);
  }
}

static void sha256_reset(void) {
  sha_h0 = 0x6a09e667; sha_h1 = 0xbb67ae85; sha_h2 = 0x3c6ef372;
  sha_h3 = 0xa54ff53a; sha_h4 = 0x510e527f; sha_h5 = 0x9b05688c;
  sha_h6 = 0x1f83d9ab; sha_h7 = 0x5be0cd19;
}
static void sha1_reset(void) {
  s1_a = 0x67452301; s1_b = 0xEFCDAB89; s1_c = 0x98BADCFE;
  s1_d = 0x10325476; s1_e = 0xC3D2E1F0;
}
static void md5_reset(void) {
  m5_a = 0x67452301; m5_b = 0xefcdab89; m5_c = 0x98badcfe; m5_d = 0x10325476;
}

static void set_be(const uint8_t *p, uint32_t *w) {
  for (int i = 0; i < 16; i++)
    w[i] = ((uint32_t)p[4 * i] << 24) | ((uint32_t)p[4 * i + 1] << 16) |
           ((uint32_t)p[4 * i + 2] << 8) | (uint32_t)p[4 * i + 3];
}
static void set_le(const uint8_t *p, uint32_t *w) {
  for (int i = 0; i < 16; i++)
    w[i] = (uint32_t)p[4 * i] | ((uint32_t)p[4 * i + 1] << 8) |
           ((uint32_t)p[4 * i + 2] << 16) | ((uint32_t)p[4 * i + 3] << 24);
}

static uint32_t wtmp[16];

static void sha256_push(const uint8_t *p) {
  set_be(p, wtmp);
  shaw0 = wtmp[0]; shaw1 = wtmp[1]; shaw2 = wtmp[2]; shaw3 = wtmp[3];
  shaw4 = wtmp[4]; shaw5 = wtmp[5]; shaw6 = wtmp[6]; shaw7 = wtmp[7];
  shaw8 = wtmp[8]; shaw9 = wtmp[9]; shaw10 = wtmp[10]; shaw11 = wtmp[11];
  shaw12 = wtmp[12]; shaw13 = wtmp[13]; shaw14 = wtmp[14];
  shaw15 = wtmp[15];
  sha256_block();
}
static void sha1_push(const uint8_t *p) {
  set_be(p, wtmp);
  s1w0 = wtmp[0]; s1w1 = wtmp[1]; s1w2 = wtmp[2]; s1w3 = wtmp[3];
  s1w4 = wtmp[4]; s1w5 = wtmp[5]; s1w6 = wtmp[6]; s1w7 = wtmp[7];
  s1w8 = wtmp[8]; s1w9 = wtmp[9]; s1w10 = wtmp[10]; s1w11 = wtmp[11];
  s1w12 = wtmp[12]; s1w13 = wtmp[13]; s1w14 = wtmp[14]; s1w15 = wtmp[15];
  sha1_block();
}
static void md5_push(const uint8_t *p) {
  set_le(p, wtmp);
  m5w0 = wtmp[0]; m5w1 = wtmp[1]; m5w2 = wtmp[2]; m5w3 = wtmp[3];
  m5w4 = wtmp[4]; m5w5 = wtmp[5]; m5w6 = wtmp[6]; m5w7 = wtmp[7];
  m5w8 = wtmp[8]; m5w9 = wtmp[9]; m5w10 = wtmp[10]; m5w11 = wtmp[11];
  m5w12 = wtmp[12]; m5w13 = wtmp[13]; m5w14 = wtmp[14]; m5w15 = wtmp[15];
  md5_block();
}

static int verify(void) {
  uint8_t abc[64];
  memset(abc, 0, sizeof abc);
  abc[0] = 'a'; abc[1] = 'b'; abc[2] = 'c'; abc[3] = 0x80;
  abc[63] = 24; /* 大端位长（sha256/sha1） */

  switch (ALGO) {
  case 1:
    return fib_recursive(32) != 2178309ULL;
  case 3:
    return fib_iterative(92) != 7540113804746346429ULL;
  case 2:
  case 5: {
    sha256_reset();
    sha256_push(abc);
    return !(sha_h0 == 0xba7816bfu && sha_h1 == 0x8f01cfeau &&
             sha_h2 == 0x414140deu && sha_h3 == 0x5dae2223u &&
             sha_h4 == 0xb00361a3u && sha_h5 == 0x96177a9cu &&
             sha_h6 == 0xb410ff61u && sha_h7 == 0xf20015adu);
  }
  case 4:
  case 8: {
    sha1_reset();
    sha1_push(abc);
    return !(s1_a == 0xa9993e36u && s1_b == 0x4706816au &&
             s1_c == 0xba3e2571u && s1_d == 0x7850c26cu && s1_e == 0x9cd0d89du);
  }
  case 6:
  case 7: {
    abc[63] = 0;
    abc[56] = 24; /* 小端位长 */
    md5_reset();
    md5_push(abc);
    return !(m5_a == 0x98500190u && m5_b == 0xb04fd23cu &&
             m5_c == 0x7d3f96d6u && m5_d == 0x727fe128u);
  }
  case 9:
  case 10: {
    /* "123456789"+7×0 的位运算 crc32，期望来自 python zlib */
    uint8_t digits[16];
    memset(digits, 0, sizeof digits);
    memcpy(digits, "123456789", 9);
    uint32_t crc = 0xFFFFFFFFu;
    for (unsigned i = 0; i < sizeof digits; i += 8) {
      uint64_t word = 0;
      for (unsigned b = 0; b < 8; b++)
        word = (word << 8) | digits[i + b];
      crc = crc32_word(crc, word);
    }
    return (crc ^ 0xFFFFFFFFu) != 0x0e8c1a27u;
  }
  }
  return 0;
}

int main(void) {
  if (verify())
    return 100 + ALGO;
  fill_buf();
  uint64_t sink = 0;
  switch (ALGO) {
  case 1:
    for (int r = 0; r < RUNS; r++)
      sink += fib_recursive(32);
    break;
  case 3:
    for (int r = 0; r < RUNS; r++)
      sink += fib_iterative(92);
    break;
  case 2:
    for (int r = 0; r < RUNS; r++) {
      sha256_reset();
      for (unsigned off = 0; off < 4096; off += 64)
        sha256_push(buf4k + off);
    }
    break;
  case 4:
    for (int r = 0; r < RUNS; r++) {
      sha1_reset();
      for (unsigned off = 0; off < 4096; off += 64)
        sha1_push(buf4k + off);
    }
    break;
  case 6:
    for (int r = 0; r < RUNS; r++) {
      md5_reset();
      for (unsigned off = 0; off < 4096; off += 64)
        md5_push(buf4k + off);
    }
    break;
  case 5:
    for (int r = 0; r < RUNS; r++) {
      sha256_reset();
      sha256_push(buf4k);
    }
    break;
  case 8:
    for (int r = 0; r < RUNS; r++) {
      sha1_reset();
      sha1_push(buf4k);
    }
    break;
  case 7:
    for (int r = 0; r < RUNS; r++) {
      md5_reset();
      md5_push(buf4k);
    }
    break;
  case 9:
  case 10: {
    unsigned bytes = ALGO == 9 ? 64 : 4096;
    for (int r = 0; r < RUNS; r++) {
      uint32_t crc = 0xFFFFFFFFu;
      for (unsigned i = 0; i < bytes; i += 8) {
        uint64_t word = 0;
        for (unsigned b = 0; b < 8; b++)
          word = (word << 8) | buf4k[i + b];
        crc = crc32_word(crc, word);
      }
      sink += crc;
    }
    break;
  }
  }
  return sink == 0xdeadbeef;
}
