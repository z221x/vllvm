#if defined(VLLVM_TEST_ICALL)
#define VLLVM_ICALL __attribute__((annotate("vllvm:icall")))
#else
#define VLLVM_ICALL
#endif

__attribute__((noinline)) static long add_bias(long a, long b) {
  return a + b + 17;
}

__attribute__((noinline)) static double mix_double(double a, double b) {
  return a * 1.5 + b * 2.25;
}

__attribute__((noinline)) static long sum_nine(
    long a0, long a1, long a2, long a3, long a4, long a5, long a6, long a7,
    long a8) {
  return a0 + a1 + a2 + a3 + a4 + a5 + a6 + a7 + a8;
}

VLLVM_ICALL __attribute__((noinline)) static long protected_integer(long seed) {
  long total = add_bias(seed, 5);
  for (long i = 0; i < 3; ++i)
    total += add_bias(total, i);
  return total + sum_nine(seed, 1, 2, 3, 4, 5, 6, 7, 8);
}

VLLVM_ICALL __attribute__((noinline)) static long protected_mixed(long seed) {
  double value = mix_double((double)seed, 3.0);
  return add_bias((long)value, seed) +
         sum_nine(9, 8, 7, 6, 5, 4, 3, 2, 1);
}

static long ctor_result;

__attribute__((constructor)) static void call_before_main(void) {
  ctor_result = protected_integer(11);
}

int main(void) {
  long a = protected_integer(11);
  long b = protected_mixed(13);
  return (ctor_result == 434 && a == 434 && b == 101) ? 0 : 1;
}
