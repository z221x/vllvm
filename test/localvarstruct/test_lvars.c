#include <stdio.h>
typedef struct Pair {
  int x;
  long long y;
  unsigned char tag;
} Pair;

typedef struct Box {
  Pair pair;
  int arr[5];
  double weight;
} Box;

static unsigned checksum_ptr(const int *ptr, int count) {
  unsigned acc = 2166136261u;
  unsigned salt = 13u;

  for (int i = 0; i < count; ++i) {
    acc ^= (unsigned)ptr[i] + salt;
    acc *= 16777619u;
    salt += ((unsigned)i ^ acc) & 7u;
  }

  return acc;
}

static unsigned fold_arrays(int seed) {
  int ints[8];
  unsigned char bytes[11];
  short lanes[5];
  unsigned acc = (unsigned)seed * 17u + 3u;

  for (int i = 0; i < 8; ++i) {
    ints[i] = seed + i * i + ((i & 1) ? -3 : 2);
    acc += (unsigned)ints[i] * (unsigned)(i + 1);
  }

  for (int i = 0; i < 11; ++i) {
    bytes[i] = (unsigned char)(acc + (unsigned)i * (unsigned)seed +
                               (unsigned)(i << 1));
    acc ^= (unsigned)bytes[i] << (unsigned)((i % 4) * 8);
  }

  int *alias = &ints[2];
  alias[3] += bytes[4];

  for (int i = 0; i < 5; ++i) {
    lanes[i] = (short)(ints[i] + bytes[i + 2] - seed);
    acc += (unsigned short)lanes[i];
  }

  return acc ^ checksum_ptr(ints, 8);
}

static long long struct_flow(int n) {
  Pair first;
  Box box;
  Box boxes[2];
  int tmp[4];

  first.x = n + 11;
  first.y = (long long)n * 97 - 41;
  first.tag = (unsigned char)(n * 3 + 5);

  box.pair = first;
  box.weight = 2.5;
  for (int i = 0; i < 5; ++i)
    box.arr[i] = first.x + i * n + (int)first.tag;

  boxes[0] = box;
  boxes[1] = box;
  boxes[1].pair.x += box.arr[3];
  boxes[1].weight += 4.0;

  int *slot = &boxes[1].arr[2];
  *slot += boxes[0].pair.tag;

  long long total = boxes[1].pair.y + boxes[1].pair.x + *slot;
  for (int i = 0; i < 4; ++i) {
    tmp[i] = boxes[i & 1].arr[i] + (int)boxes[i & 1].pair.tag;
    total += (long long)tmp[i] * (long long)(i + 1);
  }

  if (n & 1)
    return total - (long long)(boxes[1].weight * 2.0);
  return total + (long long)(boxes[0].weight * 4.0);
}

static int early_return(int n) {
  int guard = n;
  int scratch[10];

  for (int i = 0; i < 10; ++i)
    scratch[i] = n + i * guard + (i % 3);

  if (n < 0)
    return scratch[0] - scratch[1];
  if (n == 0)
    return scratch[9];

  int *cursor = scratch;
  for (int i = 0; i < 10; ++i)
    guard += cursor[i] ^ (i + 3);

  if (guard & 1)
    return guard + scratch[3];
  return guard - scratch[4];
}

static unsigned byte_shuffle(unsigned x) {
  unsigned char bytes[16];
  unsigned mix = x;
  unsigned sum = 0;

  for (int i = 0; i < 16; ++i) {
    mix = mix * 1103515245u + 12345u;
    bytes[i] = (unsigned char)(mix >> 16);
  }

  unsigned char *p = &bytes[15];
  for (int i = 0; i < 16; ++i)
    sum += (unsigned)(*p--) * (unsigned)(i + 1);

  return sum ^ mix;
}

int main(void) {
  unsigned combined = fold_arrays(9);
  combined ^= (unsigned)struct_flow(6);
  combined += (unsigned)(struct_flow(7) << 1);
  combined ^= (unsigned)early_return(-3) * 3u;
  combined += (unsigned)early_return(5) * 5u;
  combined ^= byte_shuffle(0x12345678u);
  printf("%d",combined & 127);
  return (int)(combined & 127);
}
