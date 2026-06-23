#if defined(VLLVM_TEST_BCF)
#define VLLVM_OBF __attribute__((annotate("vllvm:bcf")))
#else
#define VLLVM_OBF
#endif

VLLVM_OBF
static int bcf_target(int seed) {
  int acc = seed * 13 + 7;

  for (int i = 0; i < 19; ++i) {
    int v = seed + i * 5;
    if ((v & 3) == 0) {
      acc += (v ^ i) + 11;
    } else if ((v % 5) == 2) {
      acc -= v * 3 - i;
    } else {
      switch ((v ^ acc) & 7) {
      case 0:
      case 3:
        acc ^= v + i;
        break;
      case 1:
        acc += v << 1;
        break;
      case 4:
        acc -= v >> 1;
        break;
      default:
        acc += (v ^ (i * 9)) - 17;
        break;
      }
    }
  }

  if (acc & 1)
    return acc + seed;
  return acc - seed;
}

int main(void) {
  int result = 0;
  for (int i = 0; i < 8; ++i)
    result ^= bcf_target(i * 7 + 3);
  return result & 255;
}
