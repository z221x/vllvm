/* SHA-256（FIPS-180）单算法基准：OBF_MODE 选择标注配置，标注顶层
 * sha256_buf；启动先校验 SHA-256("abc")，再对 1MB 确定性缓冲做多轮
 * 压缩，逐轮计时并校验轮间摘要一致。 */
#include "algo_common.h"

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

static uint8_t sha_dig[32];

static void sha_round(void) { sha256_buf(g_buf, BUF_SZ, sha_dig); }
static void sha_digest(char *s) { hex(sha_dig, 32, s); }

int main(void) {
  fill_buf();
  {
    static const uint8_t ABC[3] = {'a', 'b', 'c'};
    char got[80];
    uint8_t d[32];
    sha256_buf(ABC, 3, d);
    hex(d, 32, got);
    expect("sha256", got,
           "ba7816bf8f01cfea414140de5dae2223"
           "b00361a396177a9cb410ff61f20015ad");
  }
  bench("sha256", 60, sha_round, sha_digest);
  return (int)(g_sink & 0);
}
