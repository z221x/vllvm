#include "VLLVMAttribute.h"

#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/Metadata.h"

using namespace llvm;

namespace llvm::vllvm {
namespace {
constexpr StringRef AttrPrefix = "vllvm.";
constexpr StringRef EnstrGlobalMetadata = "vllvm.enstr";

StringRef normalizeKind(StringRef Kind) {
  Kind = Kind.trim();
  Kind.consume_front("vllvm:");
  Kind.consume_front("vllvm.");
  return Kind;
}

void setOption(VLLVMOptions &Options, StringRef Kind) {
  Kind = normalizeKind(Kind);
  if (Kind == "enstr")
    Options.EncryptoStr = true;
  else if (Kind == "vmfla")
    Options.VMFlattenFunc = true;
  else if (Kind == "bcf")
    Options.BogusControlFlow = true;
  else if (Kind == "flower")
    Options.Flower = true;
  else if (Kind == "icall")
    Options.IndirectCall = true;
  else if (Kind == "ibr")
    Options.IndirectBranch = true;
  else if (Kind == "vmp")
    Options.Vmp = true;
  else
    // 未知名告警而不是静默忽略：旧别名（fla/lvars/bogus/bb2func 等）
    // 已删除，拼错或使用旧名必须对用户可见。
    errs() << "[vllvm] unknown annotation kind '" << Kind
           << "' ignored (supported: bcf,flower,vmfla,icall,enstr,ibr,vmp)\n";
}

bool isOptionSeparator(char C) {
  return C == ',' || C == ' ' || C == '+' || C == '|' || C == ';' ||
         C == '\t' || C == '\n' || C == '\r';
}

VLLVMOptions parseOptionList(StringRef Text) {
  VLLVMOptions Options;
  Text = Text.trim();
  Text.consume_front("vllvm:");

  size_t Start = 0;
  for (size_t I = 0, E = Text.size(); I <= E; ++I) {
    bool IsSep = I == E || isOptionSeparator(Text[I]);
    if (!IsSep)
      continue;
    setOption(Options, Text.slice(Start, I));
    Start = I + 1;
  }
  return Options;
}

GlobalVariable *getStringGlobal(Constant *C) {
  if (!C)
    return nullptr;
  C = C->stripPointerCasts();
  if (auto *GV = dyn_cast<GlobalVariable>(C))
    return GV;
  if (auto *CE = dyn_cast<ConstantExpr>(C)) {
    if (CE->getOpcode() == Instruction::GetElementPtr)
      return getStringGlobal(dyn_cast<Constant>(CE->getOperand(0)));
  }
  return nullptr;
}

StringRef getGlobalString(GlobalVariable *GV) {
  if (!GV || !GV->hasInitializer())
    return {};
  auto *CDA = dyn_cast<ConstantDataArray>(GV->getInitializer());
  if (!CDA || !CDA->isString())
    return {};
  return CDA->getAsCString();
}

Function *getAnnotatedFunction(Constant *C) {
  if (!C)
    return nullptr;
  C = C->stripPointerCasts();
  if (auto *F = dyn_cast<Function>(C))
    return F;
  if (auto *CE = dyn_cast<ConstantExpr>(C)) {
    if (CE->getOpcode() == Instruction::GetElementPtr)
      return dyn_cast<Function>(CE->getOperand(0)->stripPointerCasts());
  }
  return nullptr;
}

GlobalVariable *getAnnotatedGlobal(Constant *C) {
  if (!C)
    return nullptr;
  C = C->stripPointerCasts();
  if (auto *GV = dyn_cast<GlobalVariable>(C))
    return GV;
  if (auto *CE = dyn_cast<ConstantExpr>(C)) {
    if (CE->getOpcode() == Instruction::GetElementPtr)
      return getAnnotatedGlobal(dyn_cast<Constant>(CE->getOperand(0)));
  }
  return nullptr;
}

bool addOptionsAsAttributes(Function &F, const VLLVMOptions &Options) {
  bool Changed = false;
  auto AddAttr = [&](StringRef Kind, bool Enabled) {
    if (!Enabled)
      return;
    SmallString<32> AttrName(AttrPrefix);
    AttrName += Kind;
    if (!F.hasFnAttribute(AttrName)) {
      F.addFnAttr(AttrName);
      Changed = true;
    }
  };

  AddAttr("enstr", Options.EncryptoStr);
  AddAttr("vmfla", Options.VMFlattenFunc);
  AddAttr("bcf", Options.BogusControlFlow);
  AddAttr("flower", Options.Flower);
  AddAttr("icall", Options.IndirectCall);
  AddAttr("ibr", Options.IndirectBranch);
  AddAttr("vmp", Options.Vmp);
  return Changed;
}

bool addOptionsAsMetadata(GlobalVariable &GV, const VLLVMOptions &Options) {
  if (!Options.EncryptoStr || GV.getMetadata(EnstrGlobalMetadata))
    return false;

  // 变量级 enstr 不能写成 Function attribute；用 metadata 留给字符串
  // 加密 Module Pass 做目标筛选。
  GV.setMetadata(EnstrGlobalMetadata, MDNode::get(GV.getContext(), {}));
  return true;
}

bool parseAnnotationEntry(Constant *Entry) {
  auto *CS = dyn_cast<ConstantStruct>(Entry);
  if (!CS || CS->getNumOperands() < 2)
    return false;

  auto *Target = dyn_cast<Constant>(CS->getOperand(0));
  GlobalVariable *TextGV =
      getStringGlobal(dyn_cast<Constant>(CS->getOperand(1)));
  StringRef Text = getGlobalString(TextGV);
  if (!Target || Text.empty())
    return false;

  VLLVMOptions Options = parseOptionList(Text);
  if (!Options.any())
    return false;

  if (Function *F = getAnnotatedFunction(Target))
    return addOptionsAsAttributes(*F, Options);
  if (GlobalVariable *GV = getAnnotatedGlobal(Target))
    return addOptionsAsMetadata(*GV, Options);
  return false;
}
} // namespace

bool materializeAnnotationAttributes(Module &M) {
  GlobalVariable *Annotations = M.getGlobalVariable("llvm.global.annotations");
  if (!Annotations || !Annotations->hasInitializer())
    return false;

  auto *CA = dyn_cast<ConstantArray>(Annotations->getInitializer());
  if (!CA)
    return false;

  bool Changed = false;
  for (Value *Op : CA->operands())
    if (auto *C = dyn_cast<Constant>(Op))
      Changed |= parseAnnotationEntry(C);
  return Changed;
}

// 模块内是否存在任何 vllvm 标注（函数或全局对象）。用于"混淆不允许
// 与编译优化同用"的硬性检查：只探测，不物化属性。
bool moduleHasVLLVMAnnotations(Module &M) {
  GlobalVariable *Annotations = M.getGlobalVariable("llvm.global.annotations");
  if (!Annotations || !Annotations->hasInitializer())
    return false;

  auto *CA = dyn_cast<ConstantArray>(Annotations->getInitializer());
  if (!CA)
    return false;

  for (Value *Op : CA->operands()) {
    auto *CS = dyn_cast<ConstantStruct>(Op);
    if (!CS || CS->getNumOperands() < 2)
      continue;
    auto *Target = dyn_cast<Constant>(CS->getOperand(0));
    GlobalVariable *TextGV = getStringGlobal(dyn_cast<Constant>(CS->getOperand(1)));
    StringRef Text = getGlobalString(TextGV);
    if (!Target || Text.empty())
      continue;
    if (parseOptionList(Text).any())
      return true;
  }
  return false;
}

bool hasVLLVMAttribute(Function &F, StringRef Kind) {
  Kind = normalizeKind(Kind);

  SmallString<32> AttrName(AttrPrefix);
  AttrName += Kind;
  return F.hasFnAttribute(AttrName);
}

VLLVMOptions getFunctionVLLVMOptions(Function &F) {
  VLLVMOptions Options;
  Options.EncryptoStr |= hasVLLVMAttribute(F, "enstr");
  Options.VMFlattenFunc |= hasVLLVMAttribute(F, "vmfla");
  Options.BogusControlFlow |= hasVLLVMAttribute(F, "bcf");
  Options.Flower |= hasVLLVMAttribute(F, "flower");
  Options.IndirectCall |= hasVLLVMAttribute(F, "icall");
  Options.IndirectBranch |= hasVLLVMAttribute(F, "ibr");
  Options.Vmp |= hasVLLVMAttribute(F, "vmp");
  return Options;
}

bool moduleRequestsFunctionStringEncryption(Module &M) {
  for (Function &F : M)
    if (hasVLLVMAttribute(F, "enstr"))
      return true;
  return false;
}

bool hasVLLVMStringEncryptionAnnotation(GlobalVariable &GV) {
  return GV.getMetadata(EnstrGlobalMetadata) != nullptr;
}

bool moduleRequestsStringEncryption(Module &M) {
  if (moduleRequestsFunctionStringEncryption(M))
    return true;

  for (GlobalVariable &GV : M.globals())
    if (hasVLLVMStringEncryptionAnnotation(GV))
      return true;
  return false;
}
} // namespace llvm::vllvm
