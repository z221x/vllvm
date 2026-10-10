#include <stdint.h>
#include <stdio.h>
#if defined(ALGOS_VMP)
#define VMP __attribute__((annotate("vllvm:vmp")))
#else
#define VMP
#endif
#define NOINLINE __attribute__((noinline))
VMP NOINLINE uint32_t crc32_word(uint32_t crc, uint64_t word) {
#pragma clang loop vectorize(disable)
  for (int i = 7; i >= 0; i--) {
    uint32_t byte = (uint32_t)((word >> (8 * i)) & 0xff);
    crc ^= byte;
#pragma clang loop vectorize(disable)
    for (int bit = 0; bit < 8; bit++) {
      uint32_t mask = (uint32_t)-(int32_t)(crc & 1);
      crc = (crc >> 1) ^ (0xEDB88320u & mask);
    }
  }
  return crc;
}

int main(void) {
  uint32_t r = crc32_word(0xFFFFFFFFu, 0x3132333435363738ULL);
  printf("crc=%08x\n", r);
  return 0;
}
