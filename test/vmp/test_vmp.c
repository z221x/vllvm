#include <stdint.h>
#include <stdio.h>
#if !defined(_WIN32)
#include <pthread.h>
#endif

#if defined(VLLVM_TEST_VMP)
#define VMP __attribute__((annotate("vllvm:vmp")))
#define VMP_COMBINED                                                          \
  __attribute__((annotate("vllvm:vmp,enstr,vmfla,fla,icall,ibr,lvars,bcf")))
#else
#define VMP
#define VMP_COMBINED
#endif

#if defined(_WIN32)
#define NOINLINE __declspec(noinline)
#else
#define NOINLINE __attribute__((noinline))
#endif

static const char combined_text[] = "vmp-enstr-marker";

static NOINLINE uint64_t
host_call_eight(uint8_t a, int16_t b, uint32_t c, uint64_t d,
                const uint64_t *pointer, uint64_t f, uint32_t g, int8_t h) {
  return (uint64_t)a + (uint64_t)(int64_t)b * 3u + (uint64_t)c * 5u +
         d * 7u + *pointer * 11u + f * 13u + (uint64_t)g * 17u +
         (uint64_t)(int64_t)h * 19u;
}

static NOINLINE uint64_t
host_call_nine(uint64_t a0, uint64_t a1, uint64_t a2, uint64_t a3,
               uint64_t a4, uint64_t a5, uint64_t a6, uint64_t a7,
               uint64_t a8) {
  return a0 + a1 * 3u + a2 * 5u + a3 * 7u + a4 * 11u + a5 * 13u +
         a6 * 17u + a7 * 19u + a8 * 23u;
}

static NOINLINE uint64_t
host_call_ten_narrow(uint64_t a0, uint64_t a1, uint64_t a2, uint64_t a3,
                     uint64_t a4, uint64_t a5, uint64_t a6, uint64_t a7,
                     int8_t a8, uint16_t a9) {
  return a0 + a1 * 3u + a2 * 5u + a3 * 7u + a4 * 11u + a5 * 13u +
         a6 * 17u + a7 * 19u + (uint64_t)(int64_t)a8 * 23u +
         (uint64_t)a9 * 29u;
}

static NOINLINE uint64_t
host_call_fifteen(uint64_t a0, uint64_t a1, uint64_t a2, uint64_t a3,
                  uint64_t a4, uint64_t a5, uint64_t a6, uint64_t a7,
                  uint64_t a8, uint64_t a9, uint64_t a10, uint64_t a11,
                  uint64_t a12, uint64_t a13, uint64_t a14) {
  return a0 + a1 * 2u + a2 * 3u + a3 * 4u + a4 * 5u + a5 * 6u +
         a6 * 7u + a7 * 8u + a8 * 9u + a9 * 10u + a10 * 11u +
         a11 * 12u + a12 * 13u + a13 * 14u + a14 * 15u;
}

VMP uint64_t protected_mix(uint64_t a, uint64_t b) {
  uint64_t value = a * 0x100000001ULL + b;
  if (a < b)
    value ^= 0xA5A5A5A55A5A5A5AULL;
  else
    value += 17;
  return value;
}

VMP uint64_t protected_loop(uint64_t n) {
  uint64_t result = 3;
  for (uint64_t i = 0; i < n; ++i)
    result = (result * 5) ^ i;
  return result;
}

VMP uint64_t protected_memory(uint64_t *value, uint64_t delta) {
  *value = (*value + delta) ^ 0x1122334455667788ULL;
  return *value;
}

VMP uint64_t protected_switch(uint64_t value) {
  switch (value) {
  case 3:
    return 33;
  case 9:
    return 99;
  default:
    return value + 7;
  }
}

VMP uint64_t protected_pressure(uint64_t a, uint64_t b, uint64_t c,
                                uint64_t d, uint64_t e, uint64_t f) {
  uint64_t v0 = a + 0x101;
  uint64_t v1 = b ^ 0x202;
  uint64_t v2 = c * 3;
  uint64_t v3 = d + 0x404;
  uint64_t v4 = e ^ 0x505;
  uint64_t v5 = f * 7;
  uint64_t v6 = (a ^ d) + 0x606;
  uint64_t v7 = (b + e) ^ 0x707;
  uint64_t v8 = (c * f) + 0x808;
  uint64_t v9 = (a + b + c) ^ 0x909;
  uint64_t v10 = (d + e + f) * 11;
  uint64_t v11 = (a * e) ^ (b * f);
  return v0 + v1 + v2 + v3 + v4 + v5 + v6 + v7 + v8 + v9 + v10 + v11;
}

VMP int8_t protected_i8(int8_t a, int8_t b) {
  return (int8_t)((a + b) ^ (int8_t)0x5a);
}

VMP uint16_t protected_i16(uint16_t a, uint16_t b) {
  return (uint16_t)((a * 9u) + (b >> 3));
}

VMP int32_t protected_signed_i32(int32_t a, int32_t b) {
  int32_t quotient = a / b;
  int32_t remainder = a % b;
  return a < b ? (quotient + remainder) : ((a >> 3) - remainder);
}

VMP uint64_t protected_narrow_memory(uint8_t *p8, uint16_t *p16,
                                     uint32_t *p32) {
  *p8 = (uint8_t)(*p8 + 3u);
  *p16 = (uint16_t)(*p16 ^ 0x55aau);
  *p32 = *p32 + 0x10203040u;
  return (uint64_t)*p8 | ((uint64_t)*p16 << 8) | ((uint64_t)*p32 << 24);
}

VMP_COMBINED uint64_t protected_combined(uint64_t value) {
  return (value * 17u) ^ 0xc001d00du;
}

VMP uint64_t protected_hostcall(uint64_t seed, uint64_t *pointer) {
  uint64_t first = host_call_eight(
      (uint8_t)seed, (int16_t)-1234, (uint32_t)(seed + 3), seed ^ 0x55u,
      pointer, seed * 9u, (uint32_t)(seed + 0x1234u), (int8_t)-17);
  uint64_t second = host_call_eight(
      (uint8_t)(seed + 1), (int16_t)4321, (uint32_t)(seed + 5), seed + 7,
      pointer, seed * 11u, (uint32_t)(seed + 0x5678u), (int8_t)23);
  uint64_t third = host_call_nine(seed, seed + 1, seed + 2, seed + 3,
                                  seed + 4, seed + 5, seed + 6, seed + 7,
                                  seed + 8);
  uint64_t fourth = host_call_ten_narrow(
      seed, seed + 1, seed + 2, seed + 3, seed + 4, seed + 5, seed + 6,
      seed + 7, (int8_t)-31, (uint16_t)(seed + 0x7654u));
  uint64_t fifth = host_call_fifteen(
      seed, seed + 1, seed + 2, seed + 3, seed + 4, seed + 5, seed + 6,
      seed + 7, seed + 8, seed + 9, seed + 10, seed + 11, seed + 12,
      seed + 13, seed + 14);
  return first ^ second ^ third ^ fourth ^ fifth;
}

#if !defined(_WIN32)
struct thread_input {
  uint64_t seed;
  uint64_t result;
};

static void *run_protected_concurrently(void *opaque) {
  struct thread_input *input = (struct thread_input *)opaque;
  uint64_t value = input->seed;
  for (uint64_t index = 0; index != 1000; ++index)
    value ^= protected_mix(value + index, index * 13u + 7u);
  input->result = value;
  return 0;
}

static uint64_t concurrent_checksum(void) {
  pthread_t threads[4];
  struct thread_input inputs[4] = {{1, 0}, {2, 0}, {3, 0}, {4, 0}};
  uint64_t checksum = 0;
  for (unsigned index = 0; index != 4; ++index)
    if (pthread_create(&threads[index], 0, run_protected_concurrently,
                       &inputs[index]) != 0)
      return UINT64_MAX;
  for (unsigned index = 0; index != 4; ++index) {
    if (pthread_join(threads[index], 0) != 0)
      return UINT64_MAX;
    checksum ^= inputs[index].result;
  }
  return checksum;
}
#else
static uint64_t concurrent_checksum(void) { return 0; }
#endif

int main(void) {
  uint64_t memory = 0x123456789ABCDEF0ULL;
  uint8_t memory8 = 0xf9u;
  uint16_t memory16 = 0x1234u;
  uint32_t memory32 = 0x89abcdefu;
  uint64_t a = protected_mix(7, 19);
  uint64_t b = protected_loop(13);
  uint64_t c = protected_memory(&memory, 0x55AAULL);
  uint64_t d = protected_switch(9);
  uint64_t e = protected_pressure(1, 2, 3, 4, 5, 6);
  int8_t f = protected_i8(-101, 37);
  uint16_t g = protected_i16(0xf123u, 0x8abcu);
  int32_t h = protected_signed_i32(-123456789, 97);
  uint64_t i = protected_narrow_memory(&memory8, &memory16, &memory32);
  uint64_t j = concurrent_checksum();
  uint64_t k = protected_combined(0x12345678u);
  uint64_t l = protected_hostcall(0x10203u, &memory);
  printf("%016llx %016llx %016llx %016llx %016llx %d %u %d %016llx %016llx %016llx %016llx %s\n",
         (unsigned long long)a,
         (unsigned long long)b, (unsigned long long)c,
         (unsigned long long)d, (unsigned long long)e, (int)f, (unsigned)g,
         h, (unsigned long long)i, (unsigned long long)j,
         (unsigned long long)k, (unsigned long long)l, combined_text);
  return 0;
}
