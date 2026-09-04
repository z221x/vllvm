#include <string.h>

const char *pick_string(_Bool flag);
const char *pick_pointer(_Bool flag);
const char *duplicate_edges(int index);
const char *loop_phi(int limit);

int main(void) {
  for (int i = 0; i < 8; ++i) {
    if (strcmp(pick_string(i & 1), (i & 1) ? "left" : "right") ||
        strcmp(pick_pointer(i & 1), (i & 1) ? "left" : "right") ||
        strcmp(duplicate_edges(i), i < 2 ? "left" : "right") ||
        strcmp(loop_phi(i), "left"))
      return 1;
  }
  return 0;
}
