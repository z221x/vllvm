#if defined(_WIN32)
#define NOINLINE __declspec(noinline)
#else
#define NOINLINE __attribute__((noinline))
#endif
#define VMP NOINLINE __attribute__((annotate("vllvm:vmp")))

struct Pair {
  long long first;
  long long second;
};

VMP long long rejected_indirect_call(long long (*helper)(long long),
                                     long long value) {
  return helper(value);
}

VMP long long rejected_gep(long long *values, long long index) {
  return values[index];
}

VMP double rejected_float(double value) { return value + 1.0; }

VMP long long rejected_seven(long long a, long long b, long long c,
                             long long d, long long e, long long f,
                             long long g) {
  return a + b + c + d + e + f + g;
}

static NOINLINE long long
host_call_sixteen(long long a0, long long a1, long long a2, long long a3,
                  long long a4, long long a5, long long a6, long long a7,
                  long long a8, long long a9, long long a10, long long a11,
                  long long a12, long long a13, long long a14, long long a15) {
  return a0 + a1 + a2 + a3 + a4 + a5 + a6 + a7 + a8 + a9 + a10 + a11 +
         a12 + a13 + a14 + a15;
}

VMP long long rejected_hostcall_sixteen(long long value) {
  return host_call_sixteen(
      value, value + 1, value + 2, value + 3, value + 4, value + 5,
      value + 6, value + 7, value + 8, value + 9, value + 10, value + 11,
      value + 12, value + 13, value + 14, value + 15);
}

VMP struct Pair rejected_aggregate(struct Pair value) { return value; }

VMP long long rejected_volatile(volatile long long *value) { return *value; }
