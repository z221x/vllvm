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
constexpr StringRef ObfuscateAttr = "vllvm.obfuscate";
constexpr StringRef EnstrGlobalMetadata = "vllvm.enstr";

StringRef normalizeKind(StringRef Kind) {
  Kind = Kind.trim();
  Kind.consume_front("vllvm:");
  Kind.consume_front("vllvm.");
  if (Kind == "flatten")
    return "fla";
  if (Kind == "indirect-call")
    return "icall";
  if (Kind == "indirect-branch")
    return "ibr";
  if (Kind == "local-var-struct" || Kind == "localvars")
    return "lvars";
  if (Kind == "vm-flatten" || Kind == "vm_flatten" ||
      Kind == "vm-flatten-func" || Kind == "vm_flatten_func" ||
      Kind == "vm-flatten-function" || Kind == "vm_flatten_function")
    return "vmfla";
  if (Kind == "bogus-control-flow" || Kind == "bogus_control_flow" ||
      Kind == "bogus" || Kind == "fake-control-flow")
    return "bcf";
  if (Kind == "encrypt-string")
    return "enstr";
  if (Kind == "virtual-machine-protection" || Kind == "virtualize")
    return "vmp";
  return Kind;
}

void setOption(VLLVMOptions &Options, StringRef Kind) {
  Kind = normalizeKind(Kind);
  if (Kind == "enstr")
    Options.EncryptoStr = true;
  else if (Kind == "vmfla")
    Options.VMFlattenFunc = true;
  else if (Kind == "fla")
    Options.FlattenFunc = true;
  else if (Kind == "icall")
    Options.IndirectCall = true;
  else if (Kind == "ibr")
    Options.IndirectBranch = true;
  else if (Kind == "lvars")
    Options.LocalVarStruct = true;
  else if (Kind == "bcf")
    Options.BogusControlFlow = true;
  else if (Kind == "vmp")
    Options.Vmp = true;
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
  AddAttr("fla", Options.FlattenFunc);
  AddAttr("icall", Options.IndirectCall);
  AddAttr("ibr", Options.IndirectBranch);
  AddAttr("lvars", Options.LocalVarStruct);
  AddAttr("bcf", Options.BogusControlFlow);
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

bool hasVLLVMAttribute(Function &F, StringRef Kind) {
  Kind = normalizeKind(Kind);

  SmallString<32> AttrName(AttrPrefix);
  AttrName += Kind;
  if (F.hasFnAttribute(AttrName))
    return true;

  Attribute Attr = F.getFnAttribute(ObfuscateAttr);
  if (!Attr.isValid() || !Attr.isStringAttribute())
    return false;

  VLLVMOptions Options = parseOptionList(Attr.getValueAsString());
  if (Kind == "enstr")
    return Options.EncryptoStr;
  if (Kind == "vmfla")
    return Options.VMFlattenFunc;
  if (Kind == "fla")
    return Options.FlattenFunc;
  if (Kind == "icall")
    return Options.IndirectCall;
  if (Kind == "ibr")
    return Options.IndirectBranch;
  if (Kind == "lvars")
    return Options.LocalVarStruct;
  if (Kind == "bcf")
    return Options.BogusControlFlow;
  if (Kind == "vmp")
    return Options.Vmp;
  return false;
}

VLLVMOptions getFunctionVLLVMOptions(Function &F) {
  VLLVMOptions Options;
  Options.EncryptoStr |= hasVLLVMAttribute(F, "enstr");
  Options.VMFlattenFunc |= hasVLLVMAttribute(F, "vmfla");
  Options.FlattenFunc |= hasVLLVMAttribute(F, "fla");
  Options.IndirectCall |= hasVLLVMAttribute(F, "icall");
  Options.IndirectBranch |= hasVLLVMAttribute(F, "ibr");
  Options.LocalVarStruct |= hasVLLVMAttribute(F, "lvars");
  Options.BogusControlFlow |= hasVLLVMAttribute(F, "bcf");
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
