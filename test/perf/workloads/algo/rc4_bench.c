/* RC4 流加密切单算法基准：OBF_MODE 选择标注配置，标注顶层
 * rc4_xor_buf（KSA 内联）；启动先校验官方向量（key="Key"，
 * plaintext="Plaintext"），再对 1MB 确定性缓冲做多轮加密切，逐轮计时
 * 并校验轮间摘要一致。 */
#include "algo_common.h"

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

static uint8_t rc4_dig[16];
static const uint8_t RC4_KEY[3] = {'K', 'e', 'y'};

static void rc4_round(void) {
  rc4_xor_buf(g_buf, g_out, BUF_SZ, RC4_KEY, sizeof RC4_KEY);
  for (int i = 0; i < 16; i++)
    rc4_dig[i] = g_out[i] ^ g_out[BUF_SZ - 16 + i];
}
static void rc4_digest(char *s) { hex(rc4_dig, 16, s); }

int main(void) {
  fill_buf();
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
  bench("rc4", 150, rc4_round, rc4_digest);
  return (int)(g_sink & 0);
}
