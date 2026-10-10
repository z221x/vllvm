#if defined(VLLVM_TEST_ICALL)
#define VLLVM_ICALL __attribute__((annotate("vllvm:icall")))
#else
#define VLLVM_ICALL
#endif

// icall 标注在被调方：所有调用这些函数的调用点都要改走注册池跳板。
VLLVM_ICALL __attribute__((noinline)) static long add_bias(long a, long b) {
  return a + b + 17;
}

VLLVM_ICALL __attribute__((noinline)) static double mix_double(double a,
                                                               double b) {
  return a * 1.5 + b * 2.25;
}

VLLVM_ICALL __attribute__((noinline)) static long sum_nine(
    long a0, long a1, long a2, long a3, long a4, long a5, long a6, long a7,
    long a8) {
  return a0 + a1 + a2 + a3 + a4 + a5 + a6 + a7 + a8;
}

// 指针参数（x0）与整数参数（x1）都参与加密：x0~x7 的整数类寄存器均可能
// 被加密，入口经 ptrtoint/inttoptr 还原指针。
VLLVM_ICALL __attribute__((noinline)) static long ptr_mix(const long *p,
                                                          long k) {
  return p[0] + p[1] * 3 + k;
}

// 聚合参数/大结构体返回会打乱 x 寄存器序号（sret 走 x8、struct 占多枚
// x 寄存器）：这类形态之后的参数必须保持明文，不能按推算序号加密。
struct Quad {
  long a, b, c, d;
};

VLLVM_ICALL __attribute__((noinline)) static struct Quad quad_shift(
    struct Quad q, long k) {
  struct Quad r = {q.a + k, q.b + k, q.c + k, q.d + k};
  return r;
}

__attribute__((noinline)) static long protected_integer(long seed) {
  long total = add_bias(seed, 5);
  for (long i = 0; i < 3; ++i)
    total += add_bias(total, i);
  return total + sum_nine(seed, 1, 2, 3, 4, 5, 6, 7, 8);
}

__attribute__((noinline)) static long protected_mixed(long seed) {
  double value = mix_double((double)seed, 3.0);
  return add_bias((long)value, seed) +
         sum_nine(9, 8, 7, 6, 5, 4, 3, 2, 1);
}

static long ctor_result;

__attribute__((constructor)) static void call_before_main(void) {
  // 用户构造函数里的调用同样走跳板：注册必须早于用户构造函数。
  ctor_result = add_bias(11, 5);
}

int main(void) {
  long a = protected_integer(11);
  long b = protected_mixed(13);
  static const long pair[2] = {40, 1};
  long c = ptr_mix(pair, 7);
  static const struct Quad in = {1, 2, 3, 4};
  struct Quad out = quad_shift(in, 10);
  return (ctor_result == 33 && a == 434 && b == 101 && c == 50 &&
          out.a == 11 && out.b == 12 && out.c == 13 && out.d == 14)
             ? 0
             : 1;
}
