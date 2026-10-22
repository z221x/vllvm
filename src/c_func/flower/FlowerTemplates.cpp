#include "FlowerTemplates.h"

#include "llvm/ADT/StringExtras.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/InlineAsm.h"
#include "llvm/IR/Module.h"
#include "llvm/TargetParser/Triple.h"

#include <string>

using namespace llvm;

namespace llvm::vllvm::flower {
namespace {

// 注释符随 triple：AArch64 darwin（Mach-O）用 ";"，ELF（linux/android）
// 用 "//"（两者互不兼容，选错会直接汇编失败，由测试覆盖）。
StringRef asmComment(const Triple &T) { return T.isOSDarwin() ? ";" : "//"; }

// 生成一条无约束、带副作用的 inline asm 花指令调用：无操作数即无
// 寄存器分配交互（不需要 clobber 约定），副作用标记保证任何优化级别
// 都不会被消除或折叠。首行注释标记只在 IR 中可见，用于计数；注释不
// 进目标文件。
void emitAsm(IRBuilder<> &IRB, StringRef Comment, const Twine &Body) {
  InlineAsm *IA = InlineAsm::get(
      FunctionType::get(Type::getVoidTy(IRB.getContext()), false),
      (Comment + " vllvm.flower\n" + Body).str(), "",
      /*hasSideEffects=*/true);
  IRB.CreateCall(IA);
}

// 跳过型（cbz xzr）：恒真不透明条件跳转——xzr 恒为 0，cbz xzr 必然
// 跳转，但反汇编器/反编译器看到的是一条依赖寄存器值的条件分支，跳
// 过 1~2 个随机垃圾 .word。与 b 版形态互补（条件跳转 + 垃圾）。
bool emitCbzSkip(IRBuilder<> &IRB, CryptoUtils &Crypto) {
  Triple T(IRB.GetInsertBlock()->getModule()->getTargetTriple());
  if (!T.isAArch64())
    return false;
  std::string Body = "cbz xzr, 1f\n";
  unsigned Words = 1 + Crypto.getRandom32() % 2;
  for (unsigned I = 0; I < Words; ++I)
    Body += ".word 0x" + utohexstr(Crypto.getRandom32()) + "\n";
  Body += "1:\n";
  emitAsm(IRB, asmComment(T), Body);
  return true;
}

const FlowerTemplate Templates[] = {
    {"cbzskip", emitCbzSkip},
};

} // namespace

ArrayRef<FlowerTemplate> getFlowerTemplates() { return Templates; }

} // namespace llvm::vllvm::flower
