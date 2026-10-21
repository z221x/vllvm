/* CRC32（IEEE/zlib）单算法基准：OBF_MODE 选择标注配置，标注顶层
 * crc32_buf；启动先校验 CRC32("123456789")，再对 1MB 确定性缓冲做
 * 多轮计算，逐轮计时并校验轮间摘要一致。 */
#include "algo_common.h"

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
static uint32_t crc_dig;

static void crc_round(void) { crc_dig = crc32_buf(g_buf, BUF_SZ); }
static void crc_digest(char *s) { sprintf(s, "%08x", crc_dig); }

int main(void) {
  crc32_init();
  fill_buf();
  {
    static const uint8_t T[9] = {'1', '2', '3', '4', '5', '6', '7', '8',
                                 '9'};
    char got[16];
    sprintf(got, "%08x", crc32_buf(T, 9));
    expect("crc32", got, "cbf43926");
  }
  bench("crc32", 150, crc_round, crc_digest);
  return (int)(g_sink & 0);
}
