/* 标量化标准算法（VMP 测量用）：无数组、无 GEP——消息字以参数传入、
 * 状态用独立全局标量、轮函数全展开。由脚本生成，勿手改。
 * ALGOS_VMP 开启 annotate。 */
#include <stdint.h>
#if defined(ALGOS_VMP)
#define VMP __attribute__((annotate("vllvm:vmp")))
#else
#define VMP
#endif
#define NOINLINE __attribute__((noinline))

/* ---- fib ---- */
VMP NOINLINE uint64_t fib_recursive(uint64_t n) {
  if (n < 2)
    return n;
  return fib_recursive(n - 1) + fib_recursive(n - 2);
}

VMP NOINLINE uint64_t fib_iterative(uint64_t n) {
  uint64_t a = 0, b = 1;
  for (uint64_t i = 0; i < n; i++) {
    uint64_t t = a + b;
    a = b;
    b = t;
  }
  return a;
}

/* ---- crc32（位运算、无查表；一次处理一个 u64 大端字节序）---- */
VMP NOINLINE uint32_t crc32_word(uint32_t crc, uint64_t word) {
#pragma clang loop vectorize(disable)
  for (int i = 7; i >= 0; i--) {
    uint32_t byte = (uint32_t)((word >> (8 * i)) & 0xff);
    crc ^= byte;
#pragma clang loop vectorize(disable)
    for (int bit = 0; bit < 8; bit++) {
      uint32_t mask = (uint32_t)-(int32_t)(crc & 1);
      crc = (crc >> 1) ^ (0xEDB88320u & mask);
    }
  }
  return crc;
}

/* ---- sha256 ---- */

/* 空 asm 屏障：阻断 instcombine 把移位对融合成 fshl/fshr intrinsic
 * （VMP 在优化后运行，不支持漏斗移位）。base/vmp 同代码，公平。 */
#define ROTR32(x, n) (((x) >> (n)) | ((x) << (32 - (n))))
#define S256_0(x) (ROTR32(x, 2) ^ ROTR32(x, 13) ^ ROTR32(x, 22))
#define S256_1(x) (ROTR32(x, 6) ^ ROTR32(x, 11) ^ ROTR32(x, 25))
#define s256_0(x) (ROTR32(x, 7) ^ ROTR32(x, 18) ^ ((x) >> 3))
#define s256_1(x) (ROTR32(x, 17) ^ ROTR32(x, 19) ^ ((x) >> 10))
#define CH(x, y, z) (((x) & (y)) ^ (~(x) & (z)))
#define MAJ(x, y, z) (((x) & (y)) ^ ((x) & (z)) ^ ((y) & (z)))
uint32_t sha_h0, sha_h1, sha_h2, sha_h3, sha_h4, sha_h5, sha_h6, sha_h7;
uint32_t shaw0, shaw1, shaw2, shaw3, shaw4, shaw5, shaw6, shaw7, shaw8, shaw9, shaw10, shaw11, shaw12, shaw13, shaw14, shaw15;
VMP NOINLINE void sha256_block(void) {
  uint32_t w0, w1, w2, w3, w4, w5, w6, w7, w8, w9, w10, w11, w12, w13, w14, w15;
  w0 = shaw0;
  w1 = shaw1;
  w2 = shaw2;
  w3 = shaw3;
  w4 = shaw4;
  w5 = shaw5;
  w6 = shaw6;
  w7 = shaw7;
  w8 = shaw8;
  w9 = shaw9;
  w10 = shaw10;
  w11 = shaw11;
  w12 = shaw12;
  w13 = shaw13;
  w14 = shaw14;
  w15 = shaw15;

  uint32_t w16 = s256_1(w14) + w9 + s256_0(w1) + w0;
  uint32_t w17 = s256_1(w15) + w10 + s256_0(w2) + w1;
  uint32_t w18 = s256_1(w16) + w11 + s256_0(w3) + w2;
  uint32_t w19 = s256_1(w17) + w12 + s256_0(w4) + w3;
  uint32_t w20 = s256_1(w18) + w13 + s256_0(w5) + w4;
  uint32_t w21 = s256_1(w19) + w14 + s256_0(w6) + w5;
  uint32_t w22 = s256_1(w20) + w15 + s256_0(w7) + w6;
  uint32_t w23 = s256_1(w21) + w16 + s256_0(w8) + w7;
  uint32_t w24 = s256_1(w22) + w17 + s256_0(w9) + w8;
  uint32_t w25 = s256_1(w23) + w18 + s256_0(w10) + w9;
  uint32_t w26 = s256_1(w24) + w19 + s256_0(w11) + w10;
  uint32_t w27 = s256_1(w25) + w20 + s256_0(w12) + w11;
  uint32_t w28 = s256_1(w26) + w21 + s256_0(w13) + w12;
  uint32_t w29 = s256_1(w27) + w22 + s256_0(w14) + w13;
  uint32_t w30 = s256_1(w28) + w23 + s256_0(w15) + w14;
  uint32_t w31 = s256_1(w29) + w24 + s256_0(w16) + w15;
  uint32_t w32 = s256_1(w30) + w25 + s256_0(w17) + w16;
  uint32_t w33 = s256_1(w31) + w26 + s256_0(w18) + w17;
  uint32_t w34 = s256_1(w32) + w27 + s256_0(w19) + w18;
  uint32_t w35 = s256_1(w33) + w28 + s256_0(w20) + w19;
  uint32_t w36 = s256_1(w34) + w29 + s256_0(w21) + w20;
  uint32_t w37 = s256_1(w35) + w30 + s256_0(w22) + w21;
  uint32_t w38 = s256_1(w36) + w31 + s256_0(w23) + w22;
  uint32_t w39 = s256_1(w37) + w32 + s256_0(w24) + w23;
  uint32_t w40 = s256_1(w38) + w33 + s256_0(w25) + w24;
  uint32_t w41 = s256_1(w39) + w34 + s256_0(w26) + w25;
  uint32_t w42 = s256_1(w40) + w35 + s256_0(w27) + w26;
  uint32_t w43 = s256_1(w41) + w36 + s256_0(w28) + w27;
  uint32_t w44 = s256_1(w42) + w37 + s256_0(w29) + w28;
  uint32_t w45 = s256_1(w43) + w38 + s256_0(w30) + w29;
  uint32_t w46 = s256_1(w44) + w39 + s256_0(w31) + w30;
  uint32_t w47 = s256_1(w45) + w40 + s256_0(w32) + w31;
  uint32_t w48 = s256_1(w46) + w41 + s256_0(w33) + w32;
  uint32_t w49 = s256_1(w47) + w42 + s256_0(w34) + w33;
  uint32_t w50 = s256_1(w48) + w43 + s256_0(w35) + w34;
  uint32_t w51 = s256_1(w49) + w44 + s256_0(w36) + w35;
  uint32_t w52 = s256_1(w50) + w45 + s256_0(w37) + w36;
  uint32_t w53 = s256_1(w51) + w46 + s256_0(w38) + w37;
  uint32_t w54 = s256_1(w52) + w47 + s256_0(w39) + w38;
  uint32_t w55 = s256_1(w53) + w48 + s256_0(w40) + w39;
  uint32_t w56 = s256_1(w54) + w49 + s256_0(w41) + w40;
  uint32_t w57 = s256_1(w55) + w50 + s256_0(w42) + w41;
  uint32_t w58 = s256_1(w56) + w51 + s256_0(w43) + w42;
  uint32_t w59 = s256_1(w57) + w52 + s256_0(w44) + w43;
  uint32_t w60 = s256_1(w58) + w53 + s256_0(w45) + w44;
  uint32_t w61 = s256_1(w59) + w54 + s256_0(w46) + w45;
  uint32_t w62 = s256_1(w60) + w55 + s256_0(w47) + w46;
  uint32_t w63 = s256_1(w61) + w56 + s256_0(w48) + w47;
  uint32_t a = sha_h0, b = sha_h1, c = sha_h2, d = sha_h3;
  uint32_t e = sha_h4, f = sha_h5, g = sha_h6, h = sha_h7;
  { uint32_t t1 = h + S256_1(e) + CH(e, f, g) + 0x428a2f98u + w0;
    uint32_t t2 = S256_0(a) + MAJ(a, b, c);
    h = g; g = f; f = e; e = d + t1;
    d = c; c = b; b = a; a = t1 + t2; }
  { uint32_t t1 = h + S256_1(e) + CH(e, f, g) + 0x71374491u + w1;
    uint32_t t2 = S256_0(a) + MAJ(a, b, c);
    h = g; g = f; f = e; e = d + t1;
    d = c; c = b; b = a; a = t1 + t2; }
  { uint32_t t1 = h + S256_1(e) + CH(e, f, g) + 0xb5c0fbcfu + w2;
    uint32_t t2 = S256_0(a) + MAJ(a, b, c);
    h = g; g = f; f = e; e = d + t1;
    d = c; c = b; b = a; a = t1 + t2; }
  { uint32_t t1 = h + S256_1(e) + CH(e, f, g) + 0xe9b5dba5u + w3;
    uint32_t t2 = S256_0(a) + MAJ(a, b, c);
    h = g; g = f; f = e; e = d + t1;
    d = c; c = b; b = a; a = t1 + t2; }
  { uint32_t t1 = h + S256_1(e) + CH(e, f, g) + 0x3956c25bu + w4;
    uint32_t t2 = S256_0(a) + MAJ(a, b, c);
    h = g; g = f; f = e; e = d + t1;
    d = c; c = b; b = a; a = t1 + t2; }
  { uint32_t t1 = h + S256_1(e) + CH(e, f, g) + 0x59f111f1u + w5;
    uint32_t t2 = S256_0(a) + MAJ(a, b, c);
    h = g; g = f; f = e; e = d + t1;
    d = c; c = b; b = a; a = t1 + t2; }
  { uint32_t t1 = h + S256_1(e) + CH(e, f, g) + 0x923f82a4u + w6;
    uint32_t t2 = S256_0(a) + MAJ(a, b, c);
    h = g; g = f; f = e; e = d + t1;
    d = c; c = b; b = a; a = t1 + t2; }
  { uint32_t t1 = h + S256_1(e) + CH(e, f, g) + 0xab1c5ed5u + w7;
    uint32_t t2 = S256_0(a) + MAJ(a, b, c);
    h = g; g = f; f = e; e = d + t1;
    d = c; c = b; b = a; a = t1 + t2; }
  { uint32_t t1 = h + S256_1(e) + CH(e, f, g) + 0xd807aa98u + w8;
    uint32_t t2 = S256_0(a) + MAJ(a, b, c);
    h = g; g = f; f = e; e = d + t1;
    d = c; c = b; b = a; a = t1 + t2; }
  { uint32_t t1 = h + S256_1(e) + CH(e, f, g) + 0x12835b01u + w9;
    uint32_t t2 = S256_0(a) + MAJ(a, b, c);
    h = g; g = f; f = e; e = d + t1;
    d = c; c = b; b = a; a = t1 + t2; }
  { uint32_t t1 = h + S256_1(e) + CH(e, f, g) + 0x243185beu + w10;
    uint32_t t2 = S256_0(a) + MAJ(a, b, c);
    h = g; g = f; f = e; e = d + t1;
    d = c; c = b; b = a; a = t1 + t2; }
  { uint32_t t1 = h + S256_1(e) + CH(e, f, g) + 0x550c7dc3u + w11;
    uint32_t t2 = S256_0(a) + MAJ(a, b, c);
    h = g; g = f; f = e; e = d + t1;
    d = c; c = b; b = a; a = t1 + t2; }
  { uint32_t t1 = h + S256_1(e) + CH(e, f, g) + 0x72be5d74u + w12;
    uint32_t t2 = S256_0(a) + MAJ(a, b, c);
    h = g; g = f; f = e; e = d + t1;
    d = c; c = b; b = a; a = t1 + t2; }
  { uint32_t t1 = h + S256_1(e) + CH(e, f, g) + 0x80deb1feu + w13;
    uint32_t t2 = S256_0(a) + MAJ(a, b, c);
    h = g; g = f; f = e; e = d + t1;
    d = c; c = b; b = a; a = t1 + t2; }
  { uint32_t t1 = h + S256_1(e) + CH(e, f, g) + 0x9bdc06a7u + w14;
    uint32_t t2 = S256_0(a) + MAJ(a, b, c);
    h = g; g = f; f = e; e = d + t1;
    d = c; c = b; b = a; a = t1 + t2; }
  { uint32_t t1 = h + S256_1(e) + CH(e, f, g) + 0xc19bf174u + w15;
    uint32_t t2 = S256_0(a) + MAJ(a, b, c);
    h = g; g = f; f = e; e = d + t1;
    d = c; c = b; b = a; a = t1 + t2; }
  { uint32_t t1 = h + S256_1(e) + CH(e, f, g) + 0xe49b69c1u + w16;
    uint32_t t2 = S256_0(a) + MAJ(a, b, c);
    h = g; g = f; f = e; e = d + t1;
    d = c; c = b; b = a; a = t1 + t2; }
  { uint32_t t1 = h + S256_1(e) + CH(e, f, g) + 0xefbe4786u + w17;
    uint32_t t2 = S256_0(a) + MAJ(a, b, c);
    h = g; g = f; f = e; e = d + t1;
    d = c; c = b; b = a; a = t1 + t2; }
  { uint32_t t1 = h + S256_1(e) + CH(e, f, g) + 0x0fc19dc6u + w18;
    uint32_t t2 = S256_0(a) + MAJ(a, b, c);
    h = g; g = f; f = e; e = d + t1;
    d = c; c = b; b = a; a = t1 + t2; }
  { uint32_t t1 = h + S256_1(e) + CH(e, f, g) + 0x240ca1ccu + w19;
    uint32_t t2 = S256_0(a) + MAJ(a, b, c);
    h = g; g = f; f = e; e = d + t1;
    d = c; c = b; b = a; a = t1 + t2; }
  { uint32_t t1 = h + S256_1(e) + CH(e, f, g) + 0x2de92c6fu + w20;
    uint32_t t2 = S256_0(a) + MAJ(a, b, c);
    h = g; g = f; f = e; e = d + t1;
    d = c; c = b; b = a; a = t1 + t2; }
  { uint32_t t1 = h + S256_1(e) + CH(e, f, g) + 0x4a7484aau + w21;
    uint32_t t2 = S256_0(a) + MAJ(a, b, c);
    h = g; g = f; f = e; e = d + t1;
    d = c; c = b; b = a; a = t1 + t2; }
  { uint32_t t1 = h + S256_1(e) + CH(e, f, g) + 0x5cb0a9dcu + w22;
    uint32_t t2 = S256_0(a) + MAJ(a, b, c);
    h = g; g = f; f = e; e = d + t1;
    d = c; c = b; b = a; a = t1 + t2; }
  { uint32_t t1 = h + S256_1(e) + CH(e, f, g) + 0x76f988dau + w23;
    uint32_t t2 = S256_0(a) + MAJ(a, b, c);
    h = g; g = f; f = e; e = d + t1;
    d = c; c = b; b = a; a = t1 + t2; }
  { uint32_t t1 = h + S256_1(e) + CH(e, f, g) + 0x983e5152u + w24;
    uint32_t t2 = S256_0(a) + MAJ(a, b, c);
    h = g; g = f; f = e; e = d + t1;
    d = c; c = b; b = a; a = t1 + t2; }
  { uint32_t t1 = h + S256_1(e) + CH(e, f, g) + 0xa831c66du + w25;
    uint32_t t2 = S256_0(a) + MAJ(a, b, c);
    h = g; g = f; f = e; e = d + t1;
    d = c; c = b; b = a; a = t1 + t2; }
  { uint32_t t1 = h + S256_1(e) + CH(e, f, g) + 0xb00327c8u + w26;
    uint32_t t2 = S256_0(a) + MAJ(a, b, c);
    h = g; g = f; f = e; e = d + t1;
    d = c; c = b; b = a; a = t1 + t2; }
  { uint32_t t1 = h + S256_1(e) + CH(e, f, g) + 0xbf597fc7u + w27;
    uint32_t t2 = S256_0(a) + MAJ(a, b, c);
    h = g; g = f; f = e; e = d + t1;
    d = c; c = b; b = a; a = t1 + t2; }
  { uint32_t t1 = h + S256_1(e) + CH(e, f, g) + 0xc6e00bf3u + w28;
    uint32_t t2 = S256_0(a) + MAJ(a, b, c);
    h = g; g = f; f = e; e = d + t1;
    d = c; c = b; b = a; a = t1 + t2; }
  { uint32_t t1 = h + S256_1(e) + CH(e, f, g) + 0xd5a79147u + w29;
    uint32_t t2 = S256_0(a) + MAJ(a, b, c);
    h = g; g = f; f = e; e = d + t1;
    d = c; c = b; b = a; a = t1 + t2; }
  { uint32_t t1 = h + S256_1(e) + CH(e, f, g) + 0x06ca6351u + w30;
    uint32_t t2 = S256_0(a) + MAJ(a, b, c);
    h = g; g = f; f = e; e = d + t1;
    d = c; c = b; b = a; a = t1 + t2; }
  { uint32_t t1 = h + S256_1(e) + CH(e, f, g) + 0x14292967u + w31;
    uint32_t t2 = S256_0(a) + MAJ(a, b, c);
    h = g; g = f; f = e; e = d + t1;
    d = c; c = b; b = a; a = t1 + t2; }
  { uint32_t t1 = h + S256_1(e) + CH(e, f, g) + 0x27b70a85u + w32;
    uint32_t t2 = S256_0(a) + MAJ(a, b, c);
    h = g; g = f; f = e; e = d + t1;
    d = c; c = b; b = a; a = t1 + t2; }
  { uint32_t t1 = h + S256_1(e) + CH(e, f, g) + 0x2e1b2138u + w33;
    uint32_t t2 = S256_0(a) + MAJ(a, b, c);
    h = g; g = f; f = e; e = d + t1;
    d = c; c = b; b = a; a = t1 + t2; }
  { uint32_t t1 = h + S256_1(e) + CH(e, f, g) + 0x4d2c6dfcu + w34;
    uint32_t t2 = S256_0(a) + MAJ(a, b, c);
    h = g; g = f; f = e; e = d + t1;
    d = c; c = b; b = a; a = t1 + t2; }
  { uint32_t t1 = h + S256_1(e) + CH(e, f, g) + 0x53380d13u + w35;
    uint32_t t2 = S256_0(a) + MAJ(a, b, c);
    h = g; g = f; f = e; e = d + t1;
    d = c; c = b; b = a; a = t1 + t2; }
  { uint32_t t1 = h + S256_1(e) + CH(e, f, g) + 0x650a7354u + w36;
    uint32_t t2 = S256_0(a) + MAJ(a, b, c);
    h = g; g = f; f = e; e = d + t1;
    d = c; c = b; b = a; a = t1 + t2; }
  { uint32_t t1 = h + S256_1(e) + CH(e, f, g) + 0x766a0abbu + w37;
    uint32_t t2 = S256_0(a) + MAJ(a, b, c);
    h = g; g = f; f = e; e = d + t1;
    d = c; c = b; b = a; a = t1 + t2; }
  { uint32_t t1 = h + S256_1(e) + CH(e, f, g) + 0x81c2c92eu + w38;
    uint32_t t2 = S256_0(a) + MAJ(a, b, c);
    h = g; g = f; f = e; e = d + t1;
    d = c; c = b; b = a; a = t1 + t2; }
  { uint32_t t1 = h + S256_1(e) + CH(e, f, g) + 0x92722c85u + w39;
    uint32_t t2 = S256_0(a) + MAJ(a, b, c);
    h = g; g = f; f = e; e = d + t1;
    d = c; c = b; b = a; a = t1 + t2; }
  { uint32_t t1 = h + S256_1(e) + CH(e, f, g) + 0xa2bfe8a1u + w40;
    uint32_t t2 = S256_0(a) + MAJ(a, b, c);
    h = g; g = f; f = e; e = d + t1;
    d = c; c = b; b = a; a = t1 + t2; }
  { uint32_t t1 = h + S256_1(e) + CH(e, f, g) + 0xa81a664bu + w41;
    uint32_t t2 = S256_0(a) + MAJ(a, b, c);
    h = g; g = f; f = e; e = d + t1;
    d = c; c = b; b = a; a = t1 + t2; }
  { uint32_t t1 = h + S256_1(e) + CH(e, f, g) + 0xc24b8b70u + w42;
    uint32_t t2 = S256_0(a) + MAJ(a, b, c);
    h = g; g = f; f = e; e = d + t1;
    d = c; c = b; b = a; a = t1 + t2; }
  { uint32_t t1 = h + S256_1(e) + CH(e, f, g) + 0xc76c51a3u + w43;
    uint32_t t2 = S256_0(a) + MAJ(a, b, c);
    h = g; g = f; f = e; e = d + t1;
    d = c; c = b; b = a; a = t1 + t2; }
  { uint32_t t1 = h + S256_1(e) + CH(e, f, g) + 0xd192e819u + w44;
    uint32_t t2 = S256_0(a) + MAJ(a, b, c);
    h = g; g = f; f = e; e = d + t1;
    d = c; c = b; b = a; a = t1 + t2; }
  { uint32_t t1 = h + S256_1(e) + CH(e, f, g) + 0xd6990624u + w45;
    uint32_t t2 = S256_0(a) + MAJ(a, b, c);
    h = g; g = f; f = e; e = d + t1;
    d = c; c = b; b = a; a = t1 + t2; }
  { uint32_t t1 = h + S256_1(e) + CH(e, f, g) + 0xf40e3585u + w46;
    uint32_t t2 = S256_0(a) + MAJ(a, b, c);
    h = g; g = f; f = e; e = d + t1;
    d = c; c = b; b = a; a = t1 + t2; }
  { uint32_t t1 = h + S256_1(e) + CH(e, f, g) + 0x106aa070u + w47;
    uint32_t t2 = S256_0(a) + MAJ(a, b, c);
    h = g; g = f; f = e; e = d + t1;
    d = c; c = b; b = a; a = t1 + t2; }
  { uint32_t t1 = h + S256_1(e) + CH(e, f, g) + 0x19a4c116u + w48;
    uint32_t t2 = S256_0(a) + MAJ(a, b, c);
    h = g; g = f; f = e; e = d + t1;
    d = c; c = b; b = a; a = t1 + t2; }
  { uint32_t t1 = h + S256_1(e) + CH(e, f, g) + 0x1e376c08u + w49;
    uint32_t t2 = S256_0(a) + MAJ(a, b, c);
    h = g; g = f; f = e; e = d + t1;
    d = c; c = b; b = a; a = t1 + t2; }
  { uint32_t t1 = h + S256_1(e) + CH(e, f, g) + 0x2748774cu + w50;
    uint32_t t2 = S256_0(a) + MAJ(a, b, c);
    h = g; g = f; f = e; e = d + t1;
    d = c; c = b; b = a; a = t1 + t2; }
  { uint32_t t1 = h + S256_1(e) + CH(e, f, g) + 0x34b0bcb5u + w51;
    uint32_t t2 = S256_0(a) + MAJ(a, b, c);
    h = g; g = f; f = e; e = d + t1;
    d = c; c = b; b = a; a = t1 + t2; }
  { uint32_t t1 = h + S256_1(e) + CH(e, f, g) + 0x391c0cb3u + w52;
    uint32_t t2 = S256_0(a) + MAJ(a, b, c);
    h = g; g = f; f = e; e = d + t1;
    d = c; c = b; b = a; a = t1 + t2; }
  { uint32_t t1 = h + S256_1(e) + CH(e, f, g) + 0x4ed8aa4au + w53;
    uint32_t t2 = S256_0(a) + MAJ(a, b, c);
    h = g; g = f; f = e; e = d + t1;
    d = c; c = b; b = a; a = t1 + t2; }
  { uint32_t t1 = h + S256_1(e) + CH(e, f, g) + 0x5b9cca4fu + w54;
    uint32_t t2 = S256_0(a) + MAJ(a, b, c);
    h = g; g = f; f = e; e = d + t1;
    d = c; c = b; b = a; a = t1 + t2; }
  { uint32_t t1 = h + S256_1(e) + CH(e, f, g) + 0x682e6ff3u + w55;
    uint32_t t2 = S256_0(a) + MAJ(a, b, c);
    h = g; g = f; f = e; e = d + t1;
    d = c; c = b; b = a; a = t1 + t2; }
  { uint32_t t1 = h + S256_1(e) + CH(e, f, g) + 0x748f82eeu + w56;
    uint32_t t2 = S256_0(a) + MAJ(a, b, c);
    h = g; g = f; f = e; e = d + t1;
    d = c; c = b; b = a; a = t1 + t2; }
  { uint32_t t1 = h + S256_1(e) + CH(e, f, g) + 0x78a5636fu + w57;
    uint32_t t2 = S256_0(a) + MAJ(a, b, c);
    h = g; g = f; f = e; e = d + t1;
    d = c; c = b; b = a; a = t1 + t2; }
  { uint32_t t1 = h + S256_1(e) + CH(e, f, g) + 0x84c87814u + w58;
    uint32_t t2 = S256_0(a) + MAJ(a, b, c);
    h = g; g = f; f = e; e = d + t1;
    d = c; c = b; b = a; a = t1 + t2; }
  { uint32_t t1 = h + S256_1(e) + CH(e, f, g) + 0x8cc70208u + w59;
    uint32_t t2 = S256_0(a) + MAJ(a, b, c);
    h = g; g = f; f = e; e = d + t1;
    d = c; c = b; b = a; a = t1 + t2; }
  { uint32_t t1 = h + S256_1(e) + CH(e, f, g) + 0x90befffau + w60;
    uint32_t t2 = S256_0(a) + MAJ(a, b, c);
    h = g; g = f; f = e; e = d + t1;
    d = c; c = b; b = a; a = t1 + t2; }
  { uint32_t t1 = h + S256_1(e) + CH(e, f, g) + 0xa4506cebu + w61;
    uint32_t t2 = S256_0(a) + MAJ(a, b, c);
    h = g; g = f; f = e; e = d + t1;
    d = c; c = b; b = a; a = t1 + t2; }
  { uint32_t t1 = h + S256_1(e) + CH(e, f, g) + 0xbef9a3f7u + w62;
    uint32_t t2 = S256_0(a) + MAJ(a, b, c);
    h = g; g = f; f = e; e = d + t1;
    d = c; c = b; b = a; a = t1 + t2; }
  { uint32_t t1 = h + S256_1(e) + CH(e, f, g) + 0xc67178f2u + w63;
    uint32_t t2 = S256_0(a) + MAJ(a, b, c);
    h = g; g = f; f = e; e = d + t1;
    d = c; c = b; b = a; a = t1 + t2; }
  sha_h0 += a; sha_h1 += b; sha_h2 += c; sha_h3 += d;
  sha_h4 += e; sha_h5 += f; sha_h6 += g; sha_h7 += h;
}

/* ---- sha1 ---- */
#define ROTL32(x, n) (((x) << (n)) | ((x) >> (32 - (n))))
uint32_t s1_a, s1_b, s1_c, s1_d, s1_e;
uint32_t s1w0, s1w1, s1w2, s1w3, s1w4, s1w5, s1w6, s1w7, s1w8, s1w9, s1w10, s1w11, s1w12, s1w13, s1w14, s1w15;
VMP NOINLINE void sha1_block(void) {
  uint32_t w0, w1, w2, w3, w4, w5, w6, w7, w8, w9, w10, w11, w12, w13, w14, w15;
  w0 = s1w0;
  w1 = s1w1;
  w2 = s1w2;
  w3 = s1w3;
  w4 = s1w4;
  w5 = s1w5;
  w6 = s1w6;
  w7 = s1w7;
  w8 = s1w8;
  w9 = s1w9;
  w10 = s1w10;
  w11 = s1w11;
  w12 = s1w12;
  w13 = s1w13;
  w14 = s1w14;
  w15 = s1w15;

  uint32_t w16 = ROTL32(w13 ^ w8 ^ w2 ^ w0, 1);
  uint32_t w17 = ROTL32(w14 ^ w9 ^ w3 ^ w1, 1);
  uint32_t w18 = ROTL32(w15 ^ w10 ^ w4 ^ w2, 1);
  uint32_t w19 = ROTL32(w16 ^ w11 ^ w5 ^ w3, 1);
  uint32_t w20 = ROTL32(w17 ^ w12 ^ w6 ^ w4, 1);
  uint32_t w21 = ROTL32(w18 ^ w13 ^ w7 ^ w5, 1);
  uint32_t w22 = ROTL32(w19 ^ w14 ^ w8 ^ w6, 1);
  uint32_t w23 = ROTL32(w20 ^ w15 ^ w9 ^ w7, 1);
  uint32_t w24 = ROTL32(w21 ^ w16 ^ w10 ^ w8, 1);
  uint32_t w25 = ROTL32(w22 ^ w17 ^ w11 ^ w9, 1);
  uint32_t w26 = ROTL32(w23 ^ w18 ^ w12 ^ w10, 1);
  uint32_t w27 = ROTL32(w24 ^ w19 ^ w13 ^ w11, 1);
  uint32_t w28 = ROTL32(w25 ^ w20 ^ w14 ^ w12, 1);
  uint32_t w29 = ROTL32(w26 ^ w21 ^ w15 ^ w13, 1);
  uint32_t w30 = ROTL32(w27 ^ w22 ^ w16 ^ w14, 1);
  uint32_t w31 = ROTL32(w28 ^ w23 ^ w17 ^ w15, 1);
  uint32_t w32 = ROTL32(w29 ^ w24 ^ w18 ^ w16, 1);
  uint32_t w33 = ROTL32(w30 ^ w25 ^ w19 ^ w17, 1);
  uint32_t w34 = ROTL32(w31 ^ w26 ^ w20 ^ w18, 1);
  uint32_t w35 = ROTL32(w32 ^ w27 ^ w21 ^ w19, 1);
  uint32_t w36 = ROTL32(w33 ^ w28 ^ w22 ^ w20, 1);
  uint32_t w37 = ROTL32(w34 ^ w29 ^ w23 ^ w21, 1);
  uint32_t w38 = ROTL32(w35 ^ w30 ^ w24 ^ w22, 1);
  uint32_t w39 = ROTL32(w36 ^ w31 ^ w25 ^ w23, 1);
  uint32_t w40 = ROTL32(w37 ^ w32 ^ w26 ^ w24, 1);
  uint32_t w41 = ROTL32(w38 ^ w33 ^ w27 ^ w25, 1);
  uint32_t w42 = ROTL32(w39 ^ w34 ^ w28 ^ w26, 1);
  uint32_t w43 = ROTL32(w40 ^ w35 ^ w29 ^ w27, 1);
  uint32_t w44 = ROTL32(w41 ^ w36 ^ w30 ^ w28, 1);
  uint32_t w45 = ROTL32(w42 ^ w37 ^ w31 ^ w29, 1);
  uint32_t w46 = ROTL32(w43 ^ w38 ^ w32 ^ w30, 1);
  uint32_t w47 = ROTL32(w44 ^ w39 ^ w33 ^ w31, 1);
  uint32_t w48 = ROTL32(w45 ^ w40 ^ w34 ^ w32, 1);
  uint32_t w49 = ROTL32(w46 ^ w41 ^ w35 ^ w33, 1);
  uint32_t w50 = ROTL32(w47 ^ w42 ^ w36 ^ w34, 1);
  uint32_t w51 = ROTL32(w48 ^ w43 ^ w37 ^ w35, 1);
  uint32_t w52 = ROTL32(w49 ^ w44 ^ w38 ^ w36, 1);
  uint32_t w53 = ROTL32(w50 ^ w45 ^ w39 ^ w37, 1);
  uint32_t w54 = ROTL32(w51 ^ w46 ^ w40 ^ w38, 1);
  uint32_t w55 = ROTL32(w52 ^ w47 ^ w41 ^ w39, 1);
  uint32_t w56 = ROTL32(w53 ^ w48 ^ w42 ^ w40, 1);
  uint32_t w57 = ROTL32(w54 ^ w49 ^ w43 ^ w41, 1);
  uint32_t w58 = ROTL32(w55 ^ w50 ^ w44 ^ w42, 1);
  uint32_t w59 = ROTL32(w56 ^ w51 ^ w45 ^ w43, 1);
  uint32_t w60 = ROTL32(w57 ^ w52 ^ w46 ^ w44, 1);
  uint32_t w61 = ROTL32(w58 ^ w53 ^ w47 ^ w45, 1);
  uint32_t w62 = ROTL32(w59 ^ w54 ^ w48 ^ w46, 1);
  uint32_t w63 = ROTL32(w60 ^ w55 ^ w49 ^ w47, 1);
  uint32_t w64 = ROTL32(w61 ^ w56 ^ w50 ^ w48, 1);
  uint32_t w65 = ROTL32(w62 ^ w57 ^ w51 ^ w49, 1);
  uint32_t w66 = ROTL32(w63 ^ w58 ^ w52 ^ w50, 1);
  uint32_t w67 = ROTL32(w64 ^ w59 ^ w53 ^ w51, 1);
  uint32_t w68 = ROTL32(w65 ^ w60 ^ w54 ^ w52, 1);
  uint32_t w69 = ROTL32(w66 ^ w61 ^ w55 ^ w53, 1);
  uint32_t w70 = ROTL32(w67 ^ w62 ^ w56 ^ w54, 1);
  uint32_t w71 = ROTL32(w68 ^ w63 ^ w57 ^ w55, 1);
  uint32_t w72 = ROTL32(w69 ^ w64 ^ w58 ^ w56, 1);
  uint32_t w73 = ROTL32(w70 ^ w65 ^ w59 ^ w57, 1);
  uint32_t w74 = ROTL32(w71 ^ w66 ^ w60 ^ w58, 1);
  uint32_t w75 = ROTL32(w72 ^ w67 ^ w61 ^ w59, 1);
  uint32_t w76 = ROTL32(w73 ^ w68 ^ w62 ^ w60, 1);
  uint32_t w77 = ROTL32(w74 ^ w69 ^ w63 ^ w61, 1);
  uint32_t w78 = ROTL32(w75 ^ w70 ^ w64 ^ w62, 1);
  uint32_t w79 = ROTL32(w76 ^ w71 ^ w65 ^ w63, 1);
  uint32_t a = s1_a, b = s1_b, c = s1_c, d = s1_d, e = s1_e;
  { uint32_t t = ROTL32(a, 5) + ((b & c) | (~b & d)) + e + 0x5a827999u + w0;
    e = d; d = c; c = ROTL32(b, 30); b = a; a = t; }
  { uint32_t t = ROTL32(a, 5) + ((b & c) | (~b & d)) + e + 0x5a827999u + w1;
    e = d; d = c; c = ROTL32(b, 30); b = a; a = t; }
  { uint32_t t = ROTL32(a, 5) + ((b & c) | (~b & d)) + e + 0x5a827999u + w2;
    e = d; d = c; c = ROTL32(b, 30); b = a; a = t; }
  { uint32_t t = ROTL32(a, 5) + ((b & c) | (~b & d)) + e + 0x5a827999u + w3;
    e = d; d = c; c = ROTL32(b, 30); b = a; a = t; }
  { uint32_t t = ROTL32(a, 5) + ((b & c) | (~b & d)) + e + 0x5a827999u + w4;
    e = d; d = c; c = ROTL32(b, 30); b = a; a = t; }
  { uint32_t t = ROTL32(a, 5) + ((b & c) | (~b & d)) + e + 0x5a827999u + w5;
    e = d; d = c; c = ROTL32(b, 30); b = a; a = t; }
  { uint32_t t = ROTL32(a, 5) + ((b & c) | (~b & d)) + e + 0x5a827999u + w6;
    e = d; d = c; c = ROTL32(b, 30); b = a; a = t; }
  { uint32_t t = ROTL32(a, 5) + ((b & c) | (~b & d)) + e + 0x5a827999u + w7;
    e = d; d = c; c = ROTL32(b, 30); b = a; a = t; }
  { uint32_t t = ROTL32(a, 5) + ((b & c) | (~b & d)) + e + 0x5a827999u + w8;
    e = d; d = c; c = ROTL32(b, 30); b = a; a = t; }
  { uint32_t t = ROTL32(a, 5) + ((b & c) | (~b & d)) + e + 0x5a827999u + w9;
    e = d; d = c; c = ROTL32(b, 30); b = a; a = t; }
  { uint32_t t = ROTL32(a, 5) + ((b & c) | (~b & d)) + e + 0x5a827999u + w10;
    e = d; d = c; c = ROTL32(b, 30); b = a; a = t; }
  { uint32_t t = ROTL32(a, 5) + ((b & c) | (~b & d)) + e + 0x5a827999u + w11;
    e = d; d = c; c = ROTL32(b, 30); b = a; a = t; }
  { uint32_t t = ROTL32(a, 5) + ((b & c) | (~b & d)) + e + 0x5a827999u + w12;
    e = d; d = c; c = ROTL32(b, 30); b = a; a = t; }
  { uint32_t t = ROTL32(a, 5) + ((b & c) | (~b & d)) + e + 0x5a827999u + w13;
    e = d; d = c; c = ROTL32(b, 30); b = a; a = t; }
  { uint32_t t = ROTL32(a, 5) + ((b & c) | (~b & d)) + e + 0x5a827999u + w14;
    e = d; d = c; c = ROTL32(b, 30); b = a; a = t; }
  { uint32_t t = ROTL32(a, 5) + ((b & c) | (~b & d)) + e + 0x5a827999u + w15;
    e = d; d = c; c = ROTL32(b, 30); b = a; a = t; }
  { uint32_t t = ROTL32(a, 5) + ((b & c) | (~b & d)) + e + 0x5a827999u + w16;
    e = d; d = c; c = ROTL32(b, 30); b = a; a = t; }
  { uint32_t t = ROTL32(a, 5) + ((b & c) | (~b & d)) + e + 0x5a827999u + w17;
    e = d; d = c; c = ROTL32(b, 30); b = a; a = t; }
  { uint32_t t = ROTL32(a, 5) + ((b & c) | (~b & d)) + e + 0x5a827999u + w18;
    e = d; d = c; c = ROTL32(b, 30); b = a; a = t; }
  { uint32_t t = ROTL32(a, 5) + ((b & c) | (~b & d)) + e + 0x5a827999u + w19;
    e = d; d = c; c = ROTL32(b, 30); b = a; a = t; }
  { uint32_t t = ROTL32(a, 5) + (b ^ c ^ d) + e + 0x6ed9eba1u + w20;
    e = d; d = c; c = ROTL32(b, 30); b = a; a = t; }
  { uint32_t t = ROTL32(a, 5) + (b ^ c ^ d) + e + 0x6ed9eba1u + w21;
    e = d; d = c; c = ROTL32(b, 30); b = a; a = t; }
  { uint32_t t = ROTL32(a, 5) + (b ^ c ^ d) + e + 0x6ed9eba1u + w22;
    e = d; d = c; c = ROTL32(b, 30); b = a; a = t; }
  { uint32_t t = ROTL32(a, 5) + (b ^ c ^ d) + e + 0x6ed9eba1u + w23;
    e = d; d = c; c = ROTL32(b, 30); b = a; a = t; }
  { uint32_t t = ROTL32(a, 5) + (b ^ c ^ d) + e + 0x6ed9eba1u + w24;
    e = d; d = c; c = ROTL32(b, 30); b = a; a = t; }
  { uint32_t t = ROTL32(a, 5) + (b ^ c ^ d) + e + 0x6ed9eba1u + w25;
    e = d; d = c; c = ROTL32(b, 30); b = a; a = t; }
  { uint32_t t = ROTL32(a, 5) + (b ^ c ^ d) + e + 0x6ed9eba1u + w26;
    e = d; d = c; c = ROTL32(b, 30); b = a; a = t; }
  { uint32_t t = ROTL32(a, 5) + (b ^ c ^ d) + e + 0x6ed9eba1u + w27;
    e = d; d = c; c = ROTL32(b, 30); b = a; a = t; }
  { uint32_t t = ROTL32(a, 5) + (b ^ c ^ d) + e + 0x6ed9eba1u + w28;
    e = d; d = c; c = ROTL32(b, 30); b = a; a = t; }
  { uint32_t t = ROTL32(a, 5) + (b ^ c ^ d) + e + 0x6ed9eba1u + w29;
    e = d; d = c; c = ROTL32(b, 30); b = a; a = t; }
  { uint32_t t = ROTL32(a, 5) + (b ^ c ^ d) + e + 0x6ed9eba1u + w30;
    e = d; d = c; c = ROTL32(b, 30); b = a; a = t; }
  { uint32_t t = ROTL32(a, 5) + (b ^ c ^ d) + e + 0x6ed9eba1u + w31;
    e = d; d = c; c = ROTL32(b, 30); b = a; a = t; }
  { uint32_t t = ROTL32(a, 5) + (b ^ c ^ d) + e + 0x6ed9eba1u + w32;
    e = d; d = c; c = ROTL32(b, 30); b = a; a = t; }
  { uint32_t t = ROTL32(a, 5) + (b ^ c ^ d) + e + 0x6ed9eba1u + w33;
    e = d; d = c; c = ROTL32(b, 30); b = a; a = t; }
  { uint32_t t = ROTL32(a, 5) + (b ^ c ^ d) + e + 0x6ed9eba1u + w34;
    e = d; d = c; c = ROTL32(b, 30); b = a; a = t; }
  { uint32_t t = ROTL32(a, 5) + (b ^ c ^ d) + e + 0x6ed9eba1u + w35;
    e = d; d = c; c = ROTL32(b, 30); b = a; a = t; }
  { uint32_t t = ROTL32(a, 5) + (b ^ c ^ d) + e + 0x6ed9eba1u + w36;
    e = d; d = c; c = ROTL32(b, 30); b = a; a = t; }
  { uint32_t t = ROTL32(a, 5) + (b ^ c ^ d) + e + 0x6ed9eba1u + w37;
    e = d; d = c; c = ROTL32(b, 30); b = a; a = t; }
  { uint32_t t = ROTL32(a, 5) + (b ^ c ^ d) + e + 0x6ed9eba1u + w38;
    e = d; d = c; c = ROTL32(b, 30); b = a; a = t; }
  { uint32_t t = ROTL32(a, 5) + (b ^ c ^ d) + e + 0x6ed9eba1u + w39;
    e = d; d = c; c = ROTL32(b, 30); b = a; a = t; }
  { uint32_t t = ROTL32(a, 5) + ((b & c) | (b & d) | (c & d)) + e + 0x8f1bbcdcu + w40;
    e = d; d = c; c = ROTL32(b, 30); b = a; a = t; }
  { uint32_t t = ROTL32(a, 5) + ((b & c) | (b & d) | (c & d)) + e + 0x8f1bbcdcu + w41;
    e = d; d = c; c = ROTL32(b, 30); b = a; a = t; }
  { uint32_t t = ROTL32(a, 5) + ((b & c) | (b & d) | (c & d)) + e + 0x8f1bbcdcu + w42;
    e = d; d = c; c = ROTL32(b, 30); b = a; a = t; }
  { uint32_t t = ROTL32(a, 5) + ((b & c) | (b & d) | (c & d)) + e + 0x8f1bbcdcu + w43;
    e = d; d = c; c = ROTL32(b, 30); b = a; a = t; }
  { uint32_t t = ROTL32(a, 5) + ((b & c) | (b & d) | (c & d)) + e + 0x8f1bbcdcu + w44;
    e = d; d = c; c = ROTL32(b, 30); b = a; a = t; }
  { uint32_t t = ROTL32(a, 5) + ((b & c) | (b & d) | (c & d)) + e + 0x8f1bbcdcu + w45;
    e = d; d = c; c = ROTL32(b, 30); b = a; a = t; }
  { uint32_t t = ROTL32(a, 5) + ((b & c) | (b & d) | (c & d)) + e + 0x8f1bbcdcu + w46;
    e = d; d = c; c = ROTL32(b, 30); b = a; a = t; }
  { uint32_t t = ROTL32(a, 5) + ((b & c) | (b & d) | (c & d)) + e + 0x8f1bbcdcu + w47;
    e = d; d = c; c = ROTL32(b, 30); b = a; a = t; }
  { uint32_t t = ROTL32(a, 5) + ((b & c) | (b & d) | (c & d)) + e + 0x8f1bbcdcu + w48;
    e = d; d = c; c = ROTL32(b, 30); b = a; a = t; }
  { uint32_t t = ROTL32(a, 5) + ((b & c) | (b & d) | (c & d)) + e + 0x8f1bbcdcu + w49;
    e = d; d = c; c = ROTL32(b, 30); b = a; a = t; }
  { uint32_t t = ROTL32(a, 5) + ((b & c) | (b & d) | (c & d)) + e + 0x8f1bbcdcu + w50;
    e = d; d = c; c = ROTL32(b, 30); b = a; a = t; }
  { uint32_t t = ROTL32(a, 5) + ((b & c) | (b & d) | (c & d)) + e + 0x8f1bbcdcu + w51;
    e = d; d = c; c = ROTL32(b, 30); b = a; a = t; }
  { uint32_t t = ROTL32(a, 5) + ((b & c) | (b & d) | (c & d)) + e + 0x8f1bbcdcu + w52;
    e = d; d = c; c = ROTL32(b, 30); b = a; a = t; }
  { uint32_t t = ROTL32(a, 5) + ((b & c) | (b & d) | (c & d)) + e + 0x8f1bbcdcu + w53;
    e = d; d = c; c = ROTL32(b, 30); b = a; a = t; }
  { uint32_t t = ROTL32(a, 5) + ((b & c) | (b & d) | (c & d)) + e + 0x8f1bbcdcu + w54;
    e = d; d = c; c = ROTL32(b, 30); b = a; a = t; }
  { uint32_t t = ROTL32(a, 5) + ((b & c) | (b & d) | (c & d)) + e + 0x8f1bbcdcu + w55;
    e = d; d = c; c = ROTL32(b, 30); b = a; a = t; }
  { uint32_t t = ROTL32(a, 5) + ((b & c) | (b & d) | (c & d)) + e + 0x8f1bbcdcu + w56;
    e = d; d = c; c = ROTL32(b, 30); b = a; a = t; }
  { uint32_t t = ROTL32(a, 5) + ((b & c) | (b & d) | (c & d)) + e + 0x8f1bbcdcu + w57;
    e = d; d = c; c = ROTL32(b, 30); b = a; a = t; }
  { uint32_t t = ROTL32(a, 5) + ((b & c) | (b & d) | (c & d)) + e + 0x8f1bbcdcu + w58;
    e = d; d = c; c = ROTL32(b, 30); b = a; a = t; }
  { uint32_t t = ROTL32(a, 5) + ((b & c) | (b & d) | (c & d)) + e + 0x8f1bbcdcu + w59;
    e = d; d = c; c = ROTL32(b, 30); b = a; a = t; }
  { uint32_t t = ROTL32(a, 5) + (b ^ c ^ d) + e + 0xca62c1d6u + w60;
    e = d; d = c; c = ROTL32(b, 30); b = a; a = t; }
  { uint32_t t = ROTL32(a, 5) + (b ^ c ^ d) + e + 0xca62c1d6u + w61;
    e = d; d = c; c = ROTL32(b, 30); b = a; a = t; }
  { uint32_t t = ROTL32(a, 5) + (b ^ c ^ d) + e + 0xca62c1d6u + w62;
    e = d; d = c; c = ROTL32(b, 30); b = a; a = t; }
  { uint32_t t = ROTL32(a, 5) + (b ^ c ^ d) + e + 0xca62c1d6u + w63;
    e = d; d = c; c = ROTL32(b, 30); b = a; a = t; }
  { uint32_t t = ROTL32(a, 5) + (b ^ c ^ d) + e + 0xca62c1d6u + w64;
    e = d; d = c; c = ROTL32(b, 30); b = a; a = t; }
  { uint32_t t = ROTL32(a, 5) + (b ^ c ^ d) + e + 0xca62c1d6u + w65;
    e = d; d = c; c = ROTL32(b, 30); b = a; a = t; }
  { uint32_t t = ROTL32(a, 5) + (b ^ c ^ d) + e + 0xca62c1d6u + w66;
    e = d; d = c; c = ROTL32(b, 30); b = a; a = t; }
  { uint32_t t = ROTL32(a, 5) + (b ^ c ^ d) + e + 0xca62c1d6u + w67;
    e = d; d = c; c = ROTL32(b, 30); b = a; a = t; }
  { uint32_t t = ROTL32(a, 5) + (b ^ c ^ d) + e + 0xca62c1d6u + w68;
    e = d; d = c; c = ROTL32(b, 30); b = a; a = t; }
  { uint32_t t = ROTL32(a, 5) + (b ^ c ^ d) + e + 0xca62c1d6u + w69;
    e = d; d = c; c = ROTL32(b, 30); b = a; a = t; }
  { uint32_t t = ROTL32(a, 5) + (b ^ c ^ d) + e + 0xca62c1d6u + w70;
    e = d; d = c; c = ROTL32(b, 30); b = a; a = t; }
  { uint32_t t = ROTL32(a, 5) + (b ^ c ^ d) + e + 0xca62c1d6u + w71;
    e = d; d = c; c = ROTL32(b, 30); b = a; a = t; }
  { uint32_t t = ROTL32(a, 5) + (b ^ c ^ d) + e + 0xca62c1d6u + w72;
    e = d; d = c; c = ROTL32(b, 30); b = a; a = t; }
  { uint32_t t = ROTL32(a, 5) + (b ^ c ^ d) + e + 0xca62c1d6u + w73;
    e = d; d = c; c = ROTL32(b, 30); b = a; a = t; }
  { uint32_t t = ROTL32(a, 5) + (b ^ c ^ d) + e + 0xca62c1d6u + w74;
    e = d; d = c; c = ROTL32(b, 30); b = a; a = t; }
  { uint32_t t = ROTL32(a, 5) + (b ^ c ^ d) + e + 0xca62c1d6u + w75;
    e = d; d = c; c = ROTL32(b, 30); b = a; a = t; }
  { uint32_t t = ROTL32(a, 5) + (b ^ c ^ d) + e + 0xca62c1d6u + w76;
    e = d; d = c; c = ROTL32(b, 30); b = a; a = t; }
  { uint32_t t = ROTL32(a, 5) + (b ^ c ^ d) + e + 0xca62c1d6u + w77;
    e = d; d = c; c = ROTL32(b, 30); b = a; a = t; }
  { uint32_t t = ROTL32(a, 5) + (b ^ c ^ d) + e + 0xca62c1d6u + w78;
    e = d; d = c; c = ROTL32(b, 30); b = a; a = t; }
  { uint32_t t = ROTL32(a, 5) + (b ^ c ^ d) + e + 0xca62c1d6u + w79;
    e = d; d = c; c = ROTL32(b, 30); b = a; a = t; }
  s1_a += a; s1_b += b; s1_c += c; s1_d += d; s1_e += e;
}

/* ---- md5 ---- */
uint32_t m5_a, m5_b, m5_c, m5_d;
uint32_t m5w0, m5w1, m5w2, m5w3, m5w4, m5w5, m5w6, m5w7, m5w8, m5w9, m5w10, m5w11, m5w12, m5w13, m5w14, m5w15;
VMP NOINLINE void md5_block(void) {
  uint32_t w0, w1, w2, w3, w4, w5, w6, w7, w8, w9, w10, w11, w12, w13, w14, w15;
  w0 = m5w0;
  w1 = m5w1;
  w2 = m5w2;
  w3 = m5w3;
  w4 = m5w4;
  w5 = m5w5;
  w6 = m5w6;
  w7 = m5w7;
  w8 = m5w8;
  w9 = m5w9;
  w10 = m5w10;
  w11 = m5w11;
  w12 = m5w12;
  w13 = m5w13;
  w14 = m5w14;
  w15 = m5w15;

  uint32_t a = m5_a, b = m5_b, c = m5_c, d = m5_d;
  /* rounds 由生成器展开 */
#undef M5R
  { uint32_t f = ((b & c) | (~b & d)) + a + 0xd76aa478u + w0;
    a = d; d = c; c = b;
    b = b + ROTL32(f, 7); }
  { uint32_t f = ((b & c) | (~b & d)) + a + 0xe8c7b756u + w1;
    a = d; d = c; c = b;
    b = b + ROTL32(f, 12); }
  { uint32_t f = ((b & c) | (~b & d)) + a + 0x242070dbu + w2;
    a = d; d = c; c = b;
    b = b + ROTL32(f, 17); }
  { uint32_t f = ((b & c) | (~b & d)) + a + 0xc1bdceeeu + w3;
    a = d; d = c; c = b;
    b = b + ROTL32(f, 22); }
  { uint32_t f = ((b & c) | (~b & d)) + a + 0xf57c0fafu + w4;
    a = d; d = c; c = b;
    b = b + ROTL32(f, 7); }
  { uint32_t f = ((b & c) | (~b & d)) + a + 0x4787c62au + w5;
    a = d; d = c; c = b;
    b = b + ROTL32(f, 12); }
  { uint32_t f = ((b & c) | (~b & d)) + a + 0xa8304613u + w6;
    a = d; d = c; c = b;
    b = b + ROTL32(f, 17); }
  { uint32_t f = ((b & c) | (~b & d)) + a + 0xfd469501u + w7;
    a = d; d = c; c = b;
    b = b + ROTL32(f, 22); }
  { uint32_t f = ((b & c) | (~b & d)) + a + 0x698098d8u + w8;
    a = d; d = c; c = b;
    b = b + ROTL32(f, 7); }
  { uint32_t f = ((b & c) | (~b & d)) + a + 0x8b44f7afu + w9;
    a = d; d = c; c = b;
    b = b + ROTL32(f, 12); }
  { uint32_t f = ((b & c) | (~b & d)) + a + 0xffff5bb1u + w10;
    a = d; d = c; c = b;
    b = b + ROTL32(f, 17); }
  { uint32_t f = ((b & c) | (~b & d)) + a + 0x895cd7beu + w11;
    a = d; d = c; c = b;
    b = b + ROTL32(f, 22); }
  { uint32_t f = ((b & c) | (~b & d)) + a + 0x6b901122u + w12;
    a = d; d = c; c = b;
    b = b + ROTL32(f, 7); }
  { uint32_t f = ((b & c) | (~b & d)) + a + 0xfd987193u + w13;
    a = d; d = c; c = b;
    b = b + ROTL32(f, 12); }
  { uint32_t f = ((b & c) | (~b & d)) + a + 0xa679438eu + w14;
    a = d; d = c; c = b;
    b = b + ROTL32(f, 17); }
  { uint32_t f = ((b & c) | (~b & d)) + a + 0x49b40821u + w15;
    a = d; d = c; c = b;
    b = b + ROTL32(f, 22); }
  { uint32_t f = ((d & b) | (~d & c)) + a + 0xf61e2562u + w1;
    a = d; d = c; c = b;
    b = b + ROTL32(f, 5); }
  { uint32_t f = ((d & b) | (~d & c)) + a + 0xc040b340u + w6;
    a = d; d = c; c = b;
    b = b + ROTL32(f, 9); }
  { uint32_t f = ((d & b) | (~d & c)) + a + 0x265e5a51u + w11;
    a = d; d = c; c = b;
    b = b + ROTL32(f, 14); }
  { uint32_t f = ((d & b) | (~d & c)) + a + 0xe9b6c7aau + w0;
    a = d; d = c; c = b;
    b = b + ROTL32(f, 20); }
  { uint32_t f = ((d & b) | (~d & c)) + a + 0xd62f105du + w5;
    a = d; d = c; c = b;
    b = b + ROTL32(f, 5); }
  { uint32_t f = ((d & b) | (~d & c)) + a + 0x02441453u + w10;
    a = d; d = c; c = b;
    b = b + ROTL32(f, 9); }
  { uint32_t f = ((d & b) | (~d & c)) + a + 0xd8a1e681u + w15;
    a = d; d = c; c = b;
    b = b + ROTL32(f, 14); }
  { uint32_t f = ((d & b) | (~d & c)) + a + 0xe7d3fbc8u + w4;
    a = d; d = c; c = b;
    b = b + ROTL32(f, 20); }
  { uint32_t f = ((d & b) | (~d & c)) + a + 0x21e1cde6u + w9;
    a = d; d = c; c = b;
    b = b + ROTL32(f, 5); }
  { uint32_t f = ((d & b) | (~d & c)) + a + 0xc33707d6u + w14;
    a = d; d = c; c = b;
    b = b + ROTL32(f, 9); }
  { uint32_t f = ((d & b) | (~d & c)) + a + 0xf4d50d87u + w3;
    a = d; d = c; c = b;
    b = b + ROTL32(f, 14); }
  { uint32_t f = ((d & b) | (~d & c)) + a + 0x455a14edu + w8;
    a = d; d = c; c = b;
    b = b + ROTL32(f, 20); }
  { uint32_t f = ((d & b) | (~d & c)) + a + 0xa9e3e905u + w13;
    a = d; d = c; c = b;
    b = b + ROTL32(f, 5); }
  { uint32_t f = ((d & b) | (~d & c)) + a + 0xfcefa3f8u + w2;
    a = d; d = c; c = b;
    b = b + ROTL32(f, 9); }
  { uint32_t f = ((d & b) | (~d & c)) + a + 0x676f02d9u + w7;
    a = d; d = c; c = b;
    b = b + ROTL32(f, 14); }
  { uint32_t f = ((d & b) | (~d & c)) + a + 0x8d2a4c8au + w12;
    a = d; d = c; c = b;
    b = b + ROTL32(f, 20); }
  { uint32_t f = (b ^ c ^ d) + a + 0xfffa3942u + w5;
    a = d; d = c; c = b;
    b = b + ROTL32(f, 4); }
  { uint32_t f = (b ^ c ^ d) + a + 0x8771f681u + w8;
    a = d; d = c; c = b;
    b = b + ROTL32(f, 11); }
  { uint32_t f = (b ^ c ^ d) + a + 0x6d9d6122u + w11;
    a = d; d = c; c = b;
    b = b + ROTL32(f, 16); }
  { uint32_t f = (b ^ c ^ d) + a + 0xfde5380cu + w14;
    a = d; d = c; c = b;
    b = b + ROTL32(f, 23); }
  { uint32_t f = (b ^ c ^ d) + a + 0xa4beea44u + w1;
    a = d; d = c; c = b;
    b = b + ROTL32(f, 4); }
  { uint32_t f = (b ^ c ^ d) + a + 0x4bdecfa9u + w4;
    a = d; d = c; c = b;
    b = b + ROTL32(f, 11); }
  { uint32_t f = (b ^ c ^ d) + a + 0xf6bb4b60u + w7;
    a = d; d = c; c = b;
    b = b + ROTL32(f, 16); }
  { uint32_t f = (b ^ c ^ d) + a + 0xbebfbc70u + w10;
    a = d; d = c; c = b;
    b = b + ROTL32(f, 23); }
  { uint32_t f = (b ^ c ^ d) + a + 0x289b7ec6u + w13;
    a = d; d = c; c = b;
    b = b + ROTL32(f, 4); }
  { uint32_t f = (b ^ c ^ d) + a + 0xeaa127fau + w0;
    a = d; d = c; c = b;
    b = b + ROTL32(f, 11); }
  { uint32_t f = (b ^ c ^ d) + a + 0xd4ef3085u + w3;
    a = d; d = c; c = b;
    b = b + ROTL32(f, 16); }
  { uint32_t f = (b ^ c ^ d) + a + 0x04881d05u + w6;
    a = d; d = c; c = b;
    b = b + ROTL32(f, 23); }
  { uint32_t f = (b ^ c ^ d) + a + 0xd9d4d039u + w9;
    a = d; d = c; c = b;
    b = b + ROTL32(f, 4); }
  { uint32_t f = (b ^ c ^ d) + a + 0xe6db99e5u + w12;
    a = d; d = c; c = b;
    b = b + ROTL32(f, 11); }
  { uint32_t f = (b ^ c ^ d) + a + 0x1fa27cf8u + w15;
    a = d; d = c; c = b;
    b = b + ROTL32(f, 16); }
  { uint32_t f = (b ^ c ^ d) + a + 0xc4ac5665u + w2;
    a = d; d = c; c = b;
    b = b + ROTL32(f, 23); }
  { uint32_t f = (c ^ (b | ~d)) + a + 0xf4292244u + w0;
    a = d; d = c; c = b;
    b = b + ROTL32(f, 6); }
  { uint32_t f = (c ^ (b | ~d)) + a + 0x432aff97u + w7;
    a = d; d = c; c = b;
    b = b + ROTL32(f, 10); }
  { uint32_t f = (c ^ (b | ~d)) + a + 0xab9423a7u + w14;
    a = d; d = c; c = b;
    b = b + ROTL32(f, 15); }
  { uint32_t f = (c ^ (b | ~d)) + a + 0xfc93a039u + w5;
    a = d; d = c; c = b;
    b = b + ROTL32(f, 21); }
  { uint32_t f = (c ^ (b | ~d)) + a + 0x655b59c3u + w12;
    a = d; d = c; c = b;
    b = b + ROTL32(f, 6); }
  { uint32_t f = (c ^ (b | ~d)) + a + 0x8f0ccc92u + w3;
    a = d; d = c; c = b;
    b = b + ROTL32(f, 10); }
  { uint32_t f = (c ^ (b | ~d)) + a + 0xffeff47du + w10;
    a = d; d = c; c = b;
    b = b + ROTL32(f, 15); }
  { uint32_t f = (c ^ (b | ~d)) + a + 0x85845dd1u + w1;
    a = d; d = c; c = b;
    b = b + ROTL32(f, 21); }
  { uint32_t f = (c ^ (b | ~d)) + a + 0x6fa87e4fu + w8;
    a = d; d = c; c = b;
    b = b + ROTL32(f, 6); }
  { uint32_t f = (c ^ (b | ~d)) + a + 0xfe2ce6e0u + w15;
    a = d; d = c; c = b;
    b = b + ROTL32(f, 10); }
  { uint32_t f = (c ^ (b | ~d)) + a + 0xa3014314u + w6;
    a = d; d = c; c = b;
    b = b + ROTL32(f, 15); }
  { uint32_t f = (c ^ (b | ~d)) + a + 0x4e0811a1u + w13;
    a = d; d = c; c = b;
    b = b + ROTL32(f, 21); }
  { uint32_t f = (c ^ (b | ~d)) + a + 0xf7537e82u + w4;
    a = d; d = c; c = b;
    b = b + ROTL32(f, 6); }
  { uint32_t f = (c ^ (b | ~d)) + a + 0xbd3af235u + w11;
    a = d; d = c; c = b;
    b = b + ROTL32(f, 10); }
  { uint32_t f = (c ^ (b | ~d)) + a + 0x2ad7d2bbu + w2;
    a = d; d = c; c = b;
    b = b + ROTL32(f, 15); }
  { uint32_t f = (c ^ (b | ~d)) + a + 0xeb86d391u + w9;
    a = d; d = c; c = b;
    b = b + ROTL32(f, 21); }
  m5_a += a; m5_b += b; m5_c += c; m5_d += d;
}
