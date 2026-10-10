#include <stdio.h>

long cross_tu_target(long a, long b);
long cross_tu_entry(long seed);

static long ref_target(long a, long b) { return a * 3 + b - 7; }

static long ref_entry(long seed) {
  return ref_target(seed, seed + 1) + (seed ^ 0x5f) + 2 * 11;
}

int main(void) {
  int ok = 1;
  for (long s = -3; s < 40; ++s) {
    if (cross_tu_target(s, s * 2) != ref_target(s, s * 2))
      ok = 0;
    if (cross_tu_entry(s) != ref_entry(s))
      ok = 0;
  }
  puts(ok ? "cross-tu OK" : "cross-tu MISMATCH");
  return ok ? 0 : 1;
}
