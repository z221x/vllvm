#pragma once

// 花指令模板库：生成汇编级花指令（inline asm），供 FlowerPass 在标注
// 函数内随机位置插入。花指令的目标是干扰反汇编/反编译：垃圾字节被
// 跳转跳过（运行时永不执行，线性反汇编器却照常解码而出错）或以丢弃
// 写（xzr 目的寄存器）的算术混入真实指令流。
//
// 模板硬性约定（FlowerPass 与测试断言依赖）：
//  1. 通过一条无约束、带副作用的 inline asm 调用插入，不引用函数内
//     已有值、不改 CFG、无寄存器/内存状态影响，对宿主函数零语义影响。
//  2. asm 首行是注释标记 `<注释符> vllvm.flower`，注释符随目标 triple
//     选择——只在 .ll IR 里可见（作单次插入的计数标记），注释不进
//     目标文件，无符号/特征泄漏。
//  3. 仅支持 AArch64（darwin";"/ELF"//" 注释、b/cbz 跳过垃圾 .word、
//     xzr 目的寄存器死算术）；其他架构返回 false，调用方换用其他
//     模板，全部不满足则该次插入放弃。

#include "CryptoUtils.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/IR/IRBuilder.h"

namespace llvm::vllvm::flower {

struct FlowerTemplate {
  // 模板名，用于日志与调试。
  const char *Name;
  // 在插入点生成一条花指令；返回 false 表示目标架构不支持。
  bool (*Emit)(IRBuilder<> &IRB, CryptoUtils &Crypto);
};

ArrayRef<FlowerTemplate> getFlowerTemplates();

} // namespace llvm::vllvm::flower
