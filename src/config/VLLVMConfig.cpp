#include "config/VLLVMConfig.h"

#include "llvm/ADT/SmallVector.h"
#include "llvm/Support/CommandLine.h"

#include <algorithm>

using namespace llvm;
using namespace llvm::vllvm;

VLLVMConfig &VLLVMConfig::get() {
  static VLLVMConfig Instance;
  return Instance;
}

void VLLVMConfig::registerPassLevels(StringRef PassName, unsigned DefaultLevel,
                                     unsigned MaxLevel) {
  std::string Name = PassName.str();
  std::lock_guard<std::mutex> Lock(Mutex);
  LevelEntry &Entry = Entries[Name];
  Entry.DefaultLevel = std::min(DefaultLevel, MaxLevel);
  Entry.MaxLevel = MaxLevel;
  if (Entry.Explicit) {
    // 保留命令行的显式选择，只把越界值收敛进合法区间。
    unsigned Clamped = std::min(Entry.CurrentLevel, MaxLevel);
    if (Clamped != Entry.CurrentLevel)
      errs() << "[vllvm] VLLVMConfig:" << Name
             << ": level clamped to " << Clamped << "\n";
    Entry.CurrentLevel = Clamped;
  } else {
    Entry.CurrentLevel = Entry.DefaultLevel;
  }
  if (!Entry.Announced) {
    Entry.Announced = true;
    errs() << "[vllvm] VLLVMConfig:" << Name
           << ": level=" << Entry.CurrentLevel
           << " (default=" << Entry.DefaultLevel
           << " max=" << Entry.MaxLevel << ")\n";
  }
}

unsigned VLLVMConfig::getLevel(StringRef PassName) const {
  std::lock_guard<std::mutex> Lock(Mutex);
  auto It = Entries.find(PassName);
  if (It == Entries.end())
    return 1;
  return std::min(It->second.CurrentLevel, It->second.MaxLevel);
}

unsigned VLLVMConfig::getMaxLevel(StringRef PassName) const {
  std::lock_guard<std::mutex> Lock(Mutex);
  auto It = Entries.find(PassName);
  if (It == Entries.end())
    return 1;
  return It->second.MaxLevel;
}

bool VLLVMConfig::isEnabled(StringRef PassName) const {
  return getLevel(PassName) != 0;
}

void VLLVMConfig::setLevel(StringRef PassName, unsigned Level) {
  std::string Name = PassName.str();
  std::lock_guard<std::mutex> Lock(Mutex);
  LevelEntry &Entry = Entries[Name];
  Entry.Explicit = true;
  Entry.CurrentLevel = Level;
  // 注册前设置的等级先不钳制，等 registerPassLevels 收敛。
}

void VLLVMConfig::parseAndSet(StringRef Spec) {
  SmallVector<StringRef, 8> Items;
  Spec.split(Items, ',', -1, /*KeepEmpty=*/false);
  for (StringRef Item : Items) {
    Item = Item.trim();
    size_t Eq = Item.find('=');
    if (Eq == StringRef::npos || Eq == 0 || Eq + 1 == Item.size()) {
      errs() << "[vllvm] VLLVMConfig: ignored bad item '" << Item << "'\n";
      continue;
    }
    StringRef Name = Item.substr(0, Eq).trim();
    unsigned long long Parsed;
    if (Item.substr(Eq + 1).trim().getAsInteger(10, Parsed) ||
        Parsed > 0xffffffffULL) {
      errs() << "[vllvm] VLLVMConfig: ignored bad level in '" << Item << "'\n";
      continue;
    }
    setLevel(Name, static_cast<unsigned>(Parsed));
  }
}

void VLLVMConfig::reset() {
  std::lock_guard<std::mutex> Lock(Mutex);
  Entries.clear();
}

namespace {
// clang -mllvm -vllvm-config=<pass>=<level>[,...]
static cl::opt<std::string> VLLVMConfigSpec{
    "vllvm-config",
    cl::desc("VLLVM per-pass obfuscation levels, e.g. bcf=3,icall=2 (0=off)"),
    cl::value_desc("spec"),
    cl::Hidden,
    cl::init(""),
    cl::callback([](const std::string &Value) {
      VLLVMConfig::get().parseAndSet(Value);
    }),
};
} // namespace
