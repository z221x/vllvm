#include "CryptoUtils.h"
namespace llvm {
uint64_t CryptoUtils::getRandom64BaiscIndex(uint32_t index) {
  uint64_t tmpA = 0, tmpB = 0;
  uint32_t seed = sbox[index & 0xff] | sbox[(index >> 8) & 0xff] << 8 |
                  sbox[(index >> 16) & 0xff] << 16 |
                  sbox[(index >> 24) & 0xff] << 24;
  seed = seed ^ (randomKey[0] | randomKey[1] << 8 | randomKey[2] << 16 |
                 randomKey[3] << 24);
  srand(seed);
  tmpA ^= rand();
  tmpB ^= (randomKey[4] | randomKey[5] << 8 | randomKey[6] << 16 |
           randomKey[7] << 24);
  tmpB ^= tmpA;
  tmpA = tmpA << 32;
  tmpA ^= rand();
  tmpB ^= (randomKey[8] | randomKey[9] << 8 | randomKey[10] << 16 |
           randomKey[11] << 24);
  tmpB ^= tmpA;
  tmpA = tmpA << 32;
  tmpA ^= rand();
  tmpB ^= (randomKey[12] | randomKey[13] << 8 | randomKey[14] << 16 |
           randomKey[15] << 24);
  return (tmpA ^ tmpB);
}
} // namespace llvm
