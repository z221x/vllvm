#include "MergePass.h"

#include "CryptoUtils.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/Twine.h"
#include "llvm/IR/Attributes.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/InstrTypes.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Module.h"
#include "llvm/Transforms/Utils/Cloning.h"

#include <algorithm>
#include <map>
#include <vector>

using namespace llvm;

namespace {
constexpr unsigned MaxGroupSize = 8;
constexpr unsigned MaxMemberParams = 12;
constexpr StringLiteral MergeAttrName = "vllvm.merge";

enum class RetKind { Unsupported, Void, Int, Ptr };

struct MemberInfo {
  Function *F = nullptr;
  uint64_t Key = 0; // dispatch selector key；明文函数编号不出现在 IR 中
  CallInst *CaseCall = nullptr;
  bool Inlined = false;
};

// 只有整数(<=64 位)和 addrspace(0) 指针能装进共享 i64 槽位。
bool isPackableType(Type *T) {
  if (auto *IT = dyn_cast<IntegerType>(T))
    return IT->getBitWidth() <= 64;
  return T->isPointerTy() && T->getPointerAddressSpace() == 0;
}

RetKind classifyReturn(Function &F) {
  Type *T = F.getReturnType();
  if (T->isVoidTy())
    return RetKind::Void;
  if (!isPackableType(T))
    return RetKind::Unsupported;
  return T->isPointerTy() ? RetKind::Ptr : RetKind::Int;
}

// 成员体必须能安全内联进 dispatcher 的 case 块：禁止 invoke/indirectbr/
// address-taken 块、EH pad、动态栈状态、musttail 和自定义调用约定调用。
bool isUnsupportedTerminator(const Instruction *T) {
  return isa<InvokeInst>(T) || isa<IndirectBrInst>(T) || isa<CallBrInst>(T) ||
         isa<CatchSwitchInst>(T) || isa<CatchReturnInst>(T) ||
         isa<CleanupReturnInst>(T) || isa<ResumeInst>(T);
}

bool hasMergeSafeBody(Function &F) {
  for (BasicBlock &BB : F) {
    if (BB.hasAddressTaken() || BB.isEHPad())
      return false;
    if (isUnsupportedTerminator(BB.getTerminator()))
      return false;
    for (Instruction &I : BB) {
      if (auto *AI = dyn_cast<AllocaInst>(&I)) {
        if (AI->isArrayAllocation() || !isa<Constant>(AI->getArraySize()))
          return false;
        continue;
      }
      if (auto *II = dyn_cast<IntrinsicInst>(&I)) {
        if (II->getIntrinsicID() == Intrinsic::stacksave ||
            II->getIntrinsicID() == Intrinsic::stackrestore)
          return false;
        continue;
      }
      if (auto *CB = dyn_cast<CallBase>(&I)) {
        if (CB->isMustTailCall() || CB->getCallingConv() != CallingConv::C)
          return false;
      }
    }
  }
  return true;
}

bool isMergeEligible(Function &F) {
  if (F.isDeclaration() || F.empty() || F.isVarArg() || F.hasPersonalityFn() ||
      F.isInterposable())
    return false;
  if (classifyReturn(F) == RetKind::Unsupported)
    return false;
  if (F.arg_size() > MaxMemberParams)
    return false;
  for (Argument &Arg : F.args())
    if (!isPackableType(Arg.getType()))
      return false;
  return hasMergeSafeBody(F);
}

Value *packToI64(IRBuilder<> &B, Value *V) {
  Type *T = V->getType();
  if (T->isPointerTy())
    return B.CreatePtrToInt(V, B.getInt64Ty());
  if (auto *IT = dyn_cast<IntegerType>(T)) {
    if (IT->getBitWidth() == 64)
      return V;
    // zext/trunc 成对使用，只搬运位模式，不关心调用方语义上的符号。
    return B.CreateZExt(V, B.getInt64Ty());
  }
  return nullptr;
}

Value *unpackFromI64(IRBuilder<> &B, Value *V, Type *T) {
  if (T->isPointerTy())
    return B.CreateIntToPtr(V, T);
  if (auto *IT = dyn_cast<IntegerType>(T)) {
    if (IT->getBitWidth() == 64)
      return V;
    return B.CreateTrunc(V, IT);
  }
  return nullptr;
}

BasicBlock *createTrapBlock(Module &M, Function &Dispatcher,
                            const Twine &Name) {
  BasicBlock *TrapBB = BasicBlock::Create(M.getContext(), Name, &Dispatcher);
  IRBuilder<> B(TrapBB);
  Function *Trap = Intrinsic::getOrInsertDeclaration(&M, Intrinsic::trap);
  B.CreateCall(Trap);
  B.CreateUnreachable();
  return TrapBB;
}

// 收集模块内所有对 Member 的直接调用点；bundle/musttail/约定不一致的
// 调用点保持原状（wrapper 或原函数体会兜底）。
void collectDirectCallsites(Function &Member, CallInst *Exclude,
                            SmallVectorImpl<CallInst *> &Callsites) {
  for (User *U : Member.users()) {
    auto *CI = dyn_cast<CallInst>(U);
    if (!CI || CI == Exclude)
      continue;
    if (CI->getCalledOperand()->stripPointerCasts() != &Member)
      continue;
    if (CI->isMustTailCall() || CI->hasOperandBundles() ||
        CI->getCallingConv() != CallingConv::C)
      continue;
    Callsites.push_back(CI);
  }
}

// 成员函数被 llvm.global.annotations 引用会一直保持 use 非空；在删除
// 成员前先把它的标注项从 annotation 表里剪掉（属性已物化，元数据使命
// 完成）。被移除的聚合常量必须显式 destroyConstant，否则孤儿常量的
// 操作数引用会一直挂在成员函数上。
bool pruneAnnotationEntry(Module &M, Function *F) {
  GlobalVariable *Annotations = M.getGlobalVariable("llvm.global.annotations");
  if (!Annotations || !Annotations->hasInitializer())
    return false;
  auto *CA = dyn_cast<ConstantArray>(Annotations->getInitializer());
  if (!CA)
    return false;

  SmallVector<Constant *, 8> Kept;
  SmallVector<Constant *, 8> Removed;
  for (Value *Op : CA->operands()) {
    auto *C = dyn_cast<Constant>(Op);
    if (!C)
      return false;
    auto *CS = dyn_cast<ConstantStruct>(C);
    if (CS && CS->getNumOperands() >= 1 &&
        CS->getOperand(0)->stripPointerCasts() == F)
      Removed.push_back(CS);
    else
      Kept.push_back(C);
  }
  if (Removed.empty())
    return false;

  Constant *OldInit = CA;
  if (Kept.empty()) {
    Annotations->eraseFromParent();
  } else {
    // 全局变量类型必须与初始化器一致；缩小数组要重建同名的 appending
    // 全局并删除旧变量，不能直接 setInitializer。
    ArrayType *Ty =
        ArrayType::get(CA->getType()->getElementType(), Kept.size());
    std::string OldName = std::string(Annotations->getName());
    Annotations->setName(Twine(OldName, ".pruned"));
    auto *New = new GlobalVariable(
        M, Ty, Annotations->isConstant(), Annotations->getLinkage(),
        ConstantArray::get(Ty, Kept), OldName);
    New->setSection(Annotations->getSection());
    Annotations->eraseFromParent();
    // 旧数组和被移除的结构体成了孤儿常量，必须显式销毁，否则它们的
    // 操作数引用会一直挂在成员函数上，阻断 use_empty 判定。
    if (CA->use_empty())
      CA->destroyConstant();
    OldInit = nullptr;
  }
  if (OldInit && OldInit->use_empty())
    OldInit->destroyConstant();
  for (Constant *C : Removed)
    if (C->use_empty())
      C->destroyConstant();
  return true;
}

// 把一个组的成员融进一个 dispatcher；返回是否产生了变更。
bool emitMergeGroup(Module &M, CryptoUtils &Crypto,
                    ArrayRef<Function *> Members, StringRef GroupKey,
                    unsigned GroupIndex) {
  LLVMContext &Ctx = M.getContext();
  Type *I64Ty = Type::getInt64Ty(Ctx);
  IntegerType *Int32Ty = Type::getInt32Ty(Ctx);

  // 成员资格逐个复核，形状取组内最大参数槽数；返回类型统一成 i64。
  SmallVector<MemberInfo, MaxGroupSize> Infos;
  unsigned Slots = 1;
  bool AnyNonVoid = false;
  for (Function *F : Members) {
    if (!isMergeEligible(*F))
      continue;
    MemberInfo Info;
    Info.F = F;
    Infos.push_back(Info);
    Slots = std::max(Slots, static_cast<unsigned>(F->arg_size()));
    AnyNonVoid |= !F->getReturnType()->isVoidTy();
  }
  if (Infos.size() < 2)
    return false;

  llvm::sort(Infos, [](const MemberInfo &A, const MemberInfo &B) {
    return A.F->getName() < B.F->getName();
  });

  // 编号只在编译期存在；key = (Hi<<32) | (Hi ^ FuncID)，dispatch 侧
  // Hi^Lo 还原 FuncID，IR 中任何位置都不出现明文编号。
  for (unsigned I = 0, E = Infos.size(); I != E; ++I) {
    uint32_t Hi = Crypto.getRandom32();
    uint32_t Lo = Hi ^ static_cast<uint32_t>(I);
    Infos[I].Key = (static_cast<uint64_t>(Hi) << 32) | Lo;
  }

  SmallVector<Type *, 8> ParamTys;
  ParamTys.push_back(I64Ty);
  ParamTys.append(Slots, I64Ty);
  FunctionType *FTy = FunctionType::get(
      AnyNonVoid ? cast<Type>(I64Ty) : cast<Type>(Type::getVoidTy(Ctx)),
      ParamTys, false);
  auto NameStr =
      (Twine("vllvm.merge.") +
       (GroupKey.empty() ? Twine("shared") : Twine(GroupKey)) + "." +
       Twine(GroupIndex))
          .str();
  Function *Dispatcher =
      Function::Create(FTy, GlobalValue::InternalLinkage, NameStr, M);
  Dispatcher->addFnAttr(Attribute::NoInline);
  Dispatcher->addFnAttr(Attribute::OptimizeNone);

  // entry：解出 selector 并 switch 到成员 case；未知 key 落入 trap。
  BasicBlock *Entry = BasicBlock::Create(Ctx, "merge.dispatch", Dispatcher);
  BasicBlock *TrapBB = createTrapBlock(M, *Dispatcher, "merge.trap");
  IRBuilder<> EB(Entry);
  Value *Key = Dispatcher->getArg(0);
  Value *Hi32 = EB.CreateTrunc(EB.CreateLShr(Key, 32), Int32Ty, "merge.hi");
  Value *Lo32 = EB.CreateTrunc(Key, Int32Ty, "merge.lo");
  Value *Selector = EB.CreateXor(Hi32, Lo32, "merge.selector");
  SwitchInst *SI = EB.CreateSwitch(Selector, TrapBB, Infos.size());

  for (unsigned I = 0, E = Infos.size(); I != E; ++I) {
    Function *Member = Infos[I].F;
    BasicBlock *CaseBB =
        BasicBlock::Create(Ctx, "merge.case." + Member->getName(), Dispatcher);
    SI->addCase(ConstantInt::get(Int32Ty, I), CaseBB);

    IRBuilder<> CB(CaseBB);
    SmallVector<Value *, 8> Args;
    for (Argument &Arg : Member->args())
      Args.push_back(unpackFromI64(CB, Dispatcher->getArg(1 + Arg.getArgNo()),
                                   Arg.getType()));
    CallInst *Call = CB.CreateCall(Member, Args);
    Infos[I].CaseCall = Call;
    if (AnyNonVoid) {
      if (Member->getReturnType()->isVoidTy())
        CB.CreateRet(ConstantInt::get(I64Ty, 0));
      else
        CB.CreateRet(packToI64(CB, Call));
    } else {
      CB.CreateRetVoid();
    }
  }

  // 先改写全部直接调用点（成员体内部的成员互调也随之进入 dispatcher，
  // 运行时等价于重新分发一次），再做 case 内联。CaseCall 自身必须排除，
  // 否则 case 会变成对 dispatcher 的自递归。
  for (MemberInfo &Info : Infos) {
    SmallVector<CallInst *, 16> Callsites;
    collectDirectCallsites(*Info.F, Info.CaseCall, Callsites);
    for (CallInst *Call : Callsites) {
      IRBuilder<> B(Call);
      SmallVector<Value *, 9> Args;
      Args.push_back(ConstantInt::get(I64Ty, Info.Key));
      bool PackFailed = false;
      for (Use &U : Call->args()) {
        Value *Packed = packToI64(B, U.get());
        if (!Packed) {
          PackFailed = true;
          break;
        }
        Args.push_back(Packed);
      }
      if (PackFailed)
        continue;
      // dispatcher 签名按组内最大槽数布局，成员参数不足的槽位补零。
      while (Args.size() < 1 + static_cast<size_t>(Slots))
        Args.push_back(ConstantInt::get(I64Ty, 0));
      CallInst *New = B.CreateCall(Dispatcher, Args);
      if (!Info.F->getReturnType()->isVoidTy()) {
        Value *Unpacked = unpackFromI64(B, New, Info.F->getReturnType());
        Call->replaceAllUsesWith(Unpacked);
      }
      Call->eraseFromParent();
    }
  }

  // 成员体内联进 case 块；失败则保留直接调用（keyed 分发仍然成立），
  // 后续收尾也不能改写这种成员的函数体，否则 case 会自递归。
  for (MemberInfo &Info : Infos) {
    if (!Info.CaseCall)
      continue;
    InlineFunctionInfo IFI;
    Info.Inlined = InlineFunction(*Info.CaseCall, IFI).isSuccess();
  }

  // 收尾：无引用的内部成员直接删除；内联后仍有引用（地址被取/invoke 等）
  // 的成员体改写成转发 wrapper；内联失败的成员保持原函数体。
  for (MemberInfo &Info : Infos) {
    Function *Member = Info.F;
    Member->removeFnAttr(MergeAttrName);
    pruneAnnotationEntry(M, Member);
    if (Member->use_empty() && Member->hasLocalLinkage()) {
      Member->eraseFromParent();
      continue;
    }
    if (!Info.Inlined)
      continue;

    RetKind RK = classifyReturn(*Member);
    Member->dropAllReferences();
    while (!Member->empty())
      Member->begin()->eraseFromParent();

    BasicBlock *EntryBB = BasicBlock::Create(Ctx, "entry", Member);
    IRBuilder<> WB(EntryBB);
    SmallVector<Value *, 9> Args;
    Args.push_back(ConstantInt::get(I64Ty, Info.Key));
    for (Argument &Arg : Member->args())
      Args.push_back(packToI64(WB, &Arg));
    // dispatcher 签名按组内最大槽数布局，与调用点改写一样补齐空槽。
    while (Args.size() < 1 + static_cast<size_t>(Slots))
      Args.push_back(ConstantInt::get(I64Ty, 0));
    CallInst *Forward = WB.CreateCall(Dispatcher, Args);
    // wrapper 现在会执行任意成员体，原先基于旧函数体的内存/异常属性
    // 已经失真，全部摘除。
    Member->removeFnAttr(Attribute::NoUnwind);
    Member->removeFnAttr(Attribute::NoFree);
    Member->removeFnAttr(Attribute::ReadNone);
    Member->removeFnAttr(Attribute::ReadOnly);
    Member->removeFnAttr(Attribute::Memory);
    if (RK == RetKind::Void)
      WB.CreateRetVoid();
    else
      WB.CreateRet(unpackFromI64(WB, Forward, Member->getReturnType()));
  }

  errs() << "[vllvm] MergePass:" << Dispatcher->getName()
         << ":members=" << Infos.size() << "\n";
  return true;
}
} // namespace

PreservedAnalyses MergePass::run(Module &M, ModuleAnalysisManager &) {
  CryptoUtils Crypto(&M);

  // 按标记值分组：bb2func 链路写入源函数名（该函数的 helper 融合为组），
  // 手工标注的空值进入共享组。
  std::map<std::string, SmallVector<Function *, 8>, std::less<>> Buckets;
  for (Function &F : M) {
    Attribute Attr = F.getFnAttribute(MergeAttrName);
    if (!Attr.isValid())
      continue;
    Buckets[std::string(Attr.getValueAsString())].push_back(&F);
  }

  bool Changed = false;
  unsigned GroupIndex = 0;
  for (auto &Entry : Buckets) {
    SmallVector<Function *, 8> &Bucket = Entry.second;
    if (Bucket.size() < 2)
      continue;
    // 组上限切块，平衡 dispatcher 代码规模与模糊收益。
    for (unsigned Start = 0; Start < Bucket.size(); Start += MaxGroupSize) {
      unsigned End = std::min<unsigned>(Start + MaxGroupSize, Bucket.size());
      Changed |= emitMergeGroup(M, Crypto,
                                ArrayRef<Function *>(Bucket.data() + Start,
                                                     End - Start),
                                Entry.first, GroupIndex++);
    }
  }

  return Changed ? PreservedAnalyses::none() : PreservedAnalyses::all();
}
