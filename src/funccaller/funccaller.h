#include <stack>
#include <vector>
struct func_group {
  int group_length;
  void **func_ptr_array;
  func_group(int length) : group_length(length) {
    func_ptr_array = new void *[length];
  }
};
static std::vector<func_group> func_pool;
void register_func(int func_group_id, int func_index, void *func_ptr);
void call_func(int index);

void create_func_pool(int group_count, int group_length);

// 等待 vllvm 去修改
// 基于全部的函数数量去设置 比如 数量是z 随机构造 x * y = z x,y 都是随机的
// 随机 group_count = x
// 随机 group_length = y
static int group_count = 0;
static int group_length = 0;
