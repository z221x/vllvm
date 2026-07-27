#include "TargetInfo/VMPTargetInfo.h"
#include "VMP.h"

#include "llvm/IR/LegacyPassManager.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Support/CodeGen.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Target/TargetLoweringObjectFile.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/Transforms/VLLVM/VmpFunctionCompiler.h"

#include <optional>

using namespace llvm;

namespace {

class VMPNullObjectFile final : public TargetLoweringObjectFile {
public:
  void Initialize(MCContext &, const TargetMachine &) override {
    // llc 无条件初始化 TargetLoweringObjectFile。VMP 不创建 MC section，也不
    // 需要 MCAsmInfo，因此这里有意保持为空。
  }

  MCSection *getExplicitSectionGlobal(const GlobalObject *, SectionKind,
                                      const TargetMachine &) const override {
    return nullptr;
  }

protected:
  MCSection *SelectSectionForGlobal(const GlobalObject *, SectionKind,
                                    const TargetMachine &) const override {
    return nullptr;
  }
};

class VMPTargetMachine final : public TargetMachine {
public:
  VMPTargetMachine(const Target &T, const Triple &TT, StringRef CPU,
                   StringRef Features, const TargetOptions &Options,
                   std::optional<Reloc::Model> RM,
                   std::optional<CodeModel::Model> CM, CodeGenOptLevel OL, bool)
      : TargetMachine(T, llvm::vllvm::kVMPDataLayout, TT, CPU, Features,
                      Options) {
    this->RM = RM.value_or(Reloc::Static);
    this->CMModel = CM.value_or(CodeModel::Small);
    this->OptLevel = OL;
  }

  bool addPassesToEmitFile(PassManagerBase &PM, raw_pwrite_stream &Output,
                           raw_pwrite_stream *, CodeGenFileType, bool,
                           MachineModuleInfoWrapperPass *) override {
    // 纯 VMP 流不经过 MC/ObjectFile。该 ModulePass 直接完成 IR 指令选择、
    // 栈槽分配、PHI 边拷贝、REL32 修正和最终 64 位编码。
    PM.add(createVMPCodeEmitterPass(Output));
    return false;
  }

  TargetLoweringObjectFile *getObjFileLowering() const override {
    return &ObjectFile;
  }

private:
  mutable VMPNullObjectFile ObjectFile;
};

} // namespace

extern "C" LLVM_ABI LLVM_EXTERNAL_VISIBILITY void LLVMInitializeVMPTarget() {
  RegisterTargetMachine<VMPTargetMachine> X(getTheVMPTarget());
}

// VMP 没有汇编文本或宿主对象格式；保留初始化符号以满足 LLVM 的统一 Target
// 初始化入口。真正的编码器由 VMPTargetMachine 直接安装。
extern "C" LLVM_ABI LLVM_EXTERNAL_VISIBILITY void
LLVMInitializeVMPAsmPrinter() {}
