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

VMP struct Pair rejected_aggregate(struct Pair value) { return value; }

VMP long long rejected_volatile(volatile long long *value) { return *value; }
