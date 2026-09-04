#include <cstdio>
#include <cstring>

extern "C" const char *select_string(bool);
extern "C" int critical_edge(bool, int, int);
extern "C" int duplicate_edges(int);
extern "C" int loop_swap(int);
extern "C" int invoke_phi(bool, int);
extern "C" const char *invoke_string(bool, int);

extern "C" int phi_may_throw(int x) {
  if (x < 0)
    throw x;
  return x * 3 + 1;
}

int main() {
  for (int i = 0; i < 32; ++i) {
    bool flag = i & 1;
    if (std::strcmp(select_string(flag), flag ? "left" : "right") ||
        critical_edge(flag, i, i * 2) != (flag ? i : i * 2 + 3) ||
        duplicate_edges(i) != (i < 2 ? 7 : 9) ||
        loop_swap(i) != (i > 0 && !(i & 1) ? 2911 : 1129) ||
        invoke_phi(flag, i) != (flag ? i * 3 + 1 : 77) ||
        std::strcmp(invoke_string(flag, i), flag ? "left" : "right"))
      return 1;
  }
  if (invoke_phi(false, -1) != 77 ||
      std::strcmp(invoke_string(false, -1), "right"))
    return 1;
  int caught = 0;
  try {
    invoke_phi(true, -1);
  } catch (int x) {
    caught += x == -1;
  }
  try {
    invoke_string(true, -2);
  } catch (int x) {
    caught += x == -2;
  }
  if (caught != 2)
    return 1;
  std::puts("PASS PHI: strings, critical/duplicate edges, parallel loop, invoke/unwind");
  return 0;
}
