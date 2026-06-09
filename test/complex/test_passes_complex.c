typedef int (*local_op_t)(int, int);

static int op_add(int a, int b) { return a + b + 3; }
static int op_sub(int a, int b) { return a - b - 5; }
static int op_mix(int a, int b) { return (a ^ (b * 7)) + (a & 15); }

static unsigned score_string(const char *s, unsigned seed) {
  unsigned acc = seed ^ 0x9e3779b9u;
  for (int i = 0; s[i] != 0; ++i) {
    acc ^= (unsigned char)s[i] + (unsigned)i * 17u;
    acc = (acc << 5) | (acc >> 27);
    acc += 0x45d9f3bu;
  }
  return acc;
}

static unsigned string_flow(int n) {
  const char *alpha = "alpha:local-string-pass";
  const char *beta = "beta:branch-heavy-path";
  const char *gamma = "gamma:call-table-entry";
  const char *selected = (n & 1) ? beta : gamma;
  unsigned acc = score_string(alpha, (unsigned)n);

  acc ^= score_string(selected, acc);
  for (int i = 0; i < 3; ++i)
    acc += score_string(i == 1 ? "loop:string:middle" : "loop:string:edge",
                        acc + (unsigned)i);
  return acc;
}

static int call_flow(int seed) {
  local_op_t ops[3];
  int values[9];
  int total = seed;

  ops[0] = op_add;
  ops[1] = op_sub;
  ops[2] = op_mix;

  for (int i = 0; i < 9; ++i) {
    int lhs = total + i * 11;
    int rhs = seed - i * 3;
    values[i] = ops[i % 3](lhs, rhs);
    total ^= values[i] + ops[(i + 1) % 3](rhs, lhs);
  }

  return total + values[2] - values[7];
}

static int branch_flow(int n) {
  int acc = 0;

  for (int i = 0; i < 17; ++i) {
    int v = n + i * 5;
    if ((v & 3) == 0) {
      acc += v ^ (i + 9);
    } else if ((v % 5) == 1) {
      acc -= v * 3;
    } else {
      switch ((v ^ acc) & 7) {
      case 0:
      case 3:
        acc += v + i;
        break;
      case 1:
        acc ^= v << 1;
        break;
      case 4:
        acc -= v >> 1;
        break;
      default:
        acc += (v ^ i) - 11;
        break;
      }
    }
  }

  if (acc & 1)
    return acc + n;
  return acc - n;
}

static int recursive_flow(int n) {
  int local = n * 13 + 7;
  if (n <= 1)
    return local ^ 0x55;
  if (n & 1)
    return local + recursive_flow(n - 1);
  return local - recursive_flow(n - 2) + recursive_flow(n - 1);
}

static int local_flow(int seed) {
  struct Node {
    int a;
    short b[4];
    unsigned char tag;
  };
  struct Node nodes[3];
  int scratch[12];
  int acc = seed;

  for (int i = 0; i < 3; ++i) {
    nodes[i].a = seed + i * 19;
    nodes[i].tag = (unsigned char)(nodes[i].a ^ (i * 31));
    for (int j = 0; j < 4; ++j)
      nodes[i].b[j] = (short)(nodes[i].a + j * seed - nodes[i].tag);
  }

  for (int i = 0; i < 12; ++i) {
    struct Node *node = &nodes[i % 3];
    scratch[i] = node->a + node->b[i & 3] + node->tag + i;
    acc ^= scratch[i] * (i + 5);
  }

  int *alias = &scratch[4];
  alias[3] += nodes[2].tag;
  return acc + scratch[7] - nodes[1].b[2];
}

int main(void) {
  unsigned result = string_flow(11);
  result ^= (unsigned)call_flow(23);
  result += (unsigned)branch_flow(37);
  result ^= (unsigned)recursive_flow(6);
  result += (unsigned)local_flow(29);
  return (int)(result & 255u);
}
