/* 算法级基准公共设施：OBF_MODE 标注选择、确定性缓冲、计时与轮间
 * 摘要校验驱动。每个算法一个独立源文件/可执行文件（同目录
 * <algo>_bench.c），只包含单一算法与其官方测试向量，避免跨算法
 * 干扰。 */
#ifndef ALGO_COMMON_H
#define ALGO_COMMON_H

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
#elif OBF_MODE == 7
#define OBF __attribute__((annotate("vllvm:vmp")))
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

/* 预热取参照摘要，逐轮计时并校验轮间摘要无漂移；输出行格式
 * "<algo> times=t1,t2,..." 与 "<algo> digest=hex"。 */
static void bench(const char *name, int rounds, void (*one_round)(void),
                  void (*digest)(char *)) {
  char ref[80];
  one_round();
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

#endif /* ALGO_COMMON_H */
