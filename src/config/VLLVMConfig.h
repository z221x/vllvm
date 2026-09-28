#pragma once

#include "llvm/ADT/StringRef.h"

#include <map>
#include <mutex>
#include <string>

namespace llvm::vllvm {

// VLLVM 全局混淆等级配置。
//
// 每个 Pass 在自己的 run 入口调用 registerPassLevels 声明支持的等级区间，
// 等级的具体含义由各 Pass 自行解释（例如 bcf 用等级控制虚假流轮数与
// 覆盖率）；0 统一表示关闭该 Pass。
//
// 命令行入口（clang 经 -mllvm 转发到 cc1）：
//   clang -mllvm -vllvm-config=bcf=3,icall=2 ...
// 未注册的 pass 名先记录待注册时收敛；越界等级会被钳制到 [0, Max]。
class VLLVMConfig {
public:
  struct LevelEntry {
    unsigned DefaultLevel = 1;
    unsigned MaxLevel = 1;
    unsigned CurrentLevel = 1;
    // 是否被命令行/编程接口显式设置过；注册时保留显式值。
    bool Explicit = false;
    // 注册公告只打印一次，避免逐函数刷屏。
    bool Announced = false;
  };

  static VLLVMConfig &get();

  // 由各 Pass 自己调用：声明默认等级与最大等级。重复调用幂等，
  // 只收敛区间，不覆盖显式设置过的 CurrentLevel。
  void registerPassLevels(StringRef PassName, unsigned DefaultLevel,
                          unsigned MaxLevel);

  // 未注册的 pass 名返回默认等级 1，保证调用方行为与旧版一致。
  unsigned getLevel(StringRef PassName) const;
  unsigned getMaxLevel(StringRef PassName) const;
  bool isEnabled(StringRef PassName) const;

  // 显式设置等级（测试与编程接口用），立即钳制。
  void setLevel(StringRef PassName, unsigned Level);

  // 解析 "a=1,b=3" 形式的描述串；非法片段告警并跳过。
  void parseAndSet(StringRef Spec);

  // 清空全部状态（单测隔离用）。
  void reset();

private:
  VLLVMConfig() = default;

  mutable std::mutex Mutex;
  std::map<std::string, LevelEntry, std::less<>> Entries;
};

} // namespace llvm::vllvm
