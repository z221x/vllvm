#include "IndirectCallPass.h"

#include "VLLVMAttribute.h"

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/Twine.h"
#include "llvm/IR/Attributes.h"
#include "llvm/IR/CallingConv.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/InlineAsm.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/RandomNumberGenerator.h"
#include "llvm/TargetParser/Triple.h"
#include "llvm/Transforms/Utils/ModuleUtils.h"

#include <algorithm>
#include <cstdint>
#include <numeric>
#include <random>
#include <string>
#include <utility>
#include <vector>

using namespace llvm;

namespace {
constexpr StringLiteral AppliedAttr = "vllvm.icall.applied";
constexpr StringLiteral RuntimeAttr = "vllvm.icall.runtime";
constexpr uint32_t MaxPoolDimension = 256;
constexpr uint32_t MaxPoolFunctions =
    MaxPoolDimension * MaxPoolDimension;

struct PlannedCall {
  CallInst *Call = nullptr;
  Function *Callee = nullptr;
  unsigned LocalIndex = 0;
};

struct FunctionPlan {
  Function *Source = nullptr;
  std::vector<Function *> LocalCallees;
  DenseMap<Function *, unsigned> LocalIndexes;
  std::vector<PlannedCall> Calls;
};

struct PoolLocation {
  uint32_t Group = 0;
  uint32_t Index = 0;

  uint32_t pack() const { return (Group << 16) | Index; }
};

struct ICallRuntime {
  Function *CreatePool = nullptr;
  Function *RegisterFunc = nullptr;
  Function *CallFunc = nullptr;
};

void addRuntimeAttrs(Function &F) {
  F.addFnAttr(Attribute::NoInline);
  F.addFnAttr(Attribute::OptimizeNone);
  F.addFnAttr(Attribute::NoUnwind);
  F.addFnAttr(RuntimeAttr);
}

Function *getDirectInternalCallee(CallInst &Call) {
  if (Call.isMustTailCall() || Call.hasOperandBundles() ||
      Call.getCallingConv() != CallingConv::C ||
      Call.getFunctionType()->isVarArg())
    return nullptr;

  Function *Callee =
      dyn_cast<Function>(Call.getCalledOperand()->stripPointerCasts());
  if (!Callee || Callee->isIntrinsic() || Callee->isDeclaration() ||
      Callee->isVarArg() || Callee->getCallingConv() != CallingConv::C ||
      Callee->hasFnAttribute(RuntimeAttr))
    return nullptr;

  for (unsigned I = 0; I < Call.arg_size(); ++I)
    if (Call.paramHasAttr(I, Attribute::Nest))
      return nullptr;
  return Callee;
}

bool shouldPlanFunction(Function &F) {
  if (F.empty() || F.isDeclaration() || F.hasFnAttribute(AppliedAttr) ||
      F.hasFnAttribute(Attribute::Naked))
    return false;

  llvm::vllvm::VLLVMOptions Options =
      llvm::vllvm::getFunctionVLLVMOptions(F);
  // VMP/VMFlattenFunc 拥有组合标记的优先级，并维护各自的调用改写。
  return Options.IndirectCall && !Options.Vmp && !Options.VMFlattenFunc;
}

void collectPlans(Module &M, std::vector<FunctionPlan> &Plans,
                  std::vector<Function *> &GlobalCallees,
                  DenseMap<Function *, unsigned> &GlobalIndexes) {
  for (Function &F : M) {
    if (!shouldPlanFunction(F))
      continue;

    FunctionPlan Plan;
    Plan.Source = &F;
    for (Instruction &I : instructions(F)) {
      auto *Call = dyn_cast<CallInst>(&I);
      if (!Call)
        continue;
      Function *Callee = getDirectInternalCallee(*Call);
      if (!Callee)
        continue;

      auto [LocalIt, LocalInserted] =
          Plan.LocalIndexes.try_emplace(Callee, Plan.LocalCallees.size());
      if (LocalInserted)
        Plan.LocalCallees.push_back(Callee);
      Plan.Calls.push_back({Call, Callee, LocalIt->second});

      auto [GlobalIt, GlobalInserted] =
          GlobalIndexes.try_emplace(Callee, GlobalCallees.size());
      if (GlobalInserted)
        GlobalCallees.push_back(Callee);
      (void)GlobalIt;
    }

    if (!Plan.Calls.empty())
      Plans.push_back(std::move(Plan));
  }
}

std::pair<uint32_t, uint32_t>
choosePoolShape(size_t FunctionCount, std::default_random_engine &Engine) {
  uint32_t MinGroups = static_cast<uint32_t>(
      (FunctionCount + MaxPoolDimension - 1) / MaxPoolDimension);
  uint32_t MaxGroups = static_cast<uint32_t>(
      std::min<size_t>(FunctionCount, MaxPoolDimension));
  std::uniform_int_distribution<uint32_t> PickGroupCount(MinGroups, MaxGroups);
  uint32_t GroupCount = PickGroupCount(Engine);
  uint32_t GroupLength = static_cast<uint32_t>(
      (FunctionCount + GroupCount - 1) / GroupCount);
  return {GroupCount, GroupLength};
}

std::vector<PoolLocation>
placeCallees(size_t FunctionCount, uint32_t GroupCount, uint32_t GroupLength,
             std::default_random_engine &Engine) {
  std::vector<uint32_t> Slots(GroupCount * GroupLength);
  std::iota(Slots.begin(), Slots.end(), 0);
  std::shuffle(Slots.begin(), Slots.end(), Engine);

  std::vector<PoolLocation> Locations(FunctionCount);
  for (size_t I = 0; I < FunctionCount; ++I) {
    uint32_t Slot = Slots[I];
    Locations[I] = {Slot / GroupLength, Slot % GroupLength};
  }
  return Locations;
}

Function *createCreatePoolFunction(Module &M, StringRef Name,
                                   GlobalVariable *GroupCountState,
                                   GlobalVariable *GroupLengthState) {
  LLVMContext &Ctx = M.getContext();
  Type *I32Ty = Type::getInt32Ty(Ctx);
  Function *F = Function::Create(
      FunctionType::get(Type::getVoidTy(Ctx), {I32Ty, I32Ty}, false),
      GlobalValue::InternalLinkage, Name, M);
  addRuntimeAttrs(*F);

  auto Arg = F->arg_begin();
  Value *GroupCount = &*Arg++;
  Value *GroupLength = &*Arg;
  IRBuilder<> IRB(BasicBlock::Create(Ctx, "entry", F));
  StoreInst *CountStore = IRB.CreateStore(GroupCount, GroupCountState);
  StoreInst *LengthStore = IRB.CreateStore(GroupLength, GroupLengthState);
  CountStore->setVolatile(true);
  LengthStore->setVolatile(true);
  IRB.CreateRetVoid();
  return F;
}

Function *createRegisterFunction(Module &M, StringRef Name,
                                 StructType *GroupTy, ArrayType *GroupsTy,
                                 GlobalVariable *Groups) {
  LLVMContext &Ctx = M.getContext();
  Type *I32Ty = Type::getInt32Ty(Ctx);
  Type *PtrTy = PointerType::getUnqual(Ctx);
  Function *F = Function::Create(
      FunctionType::get(Type::getVoidTy(Ctx), {I32Ty, I32Ty, PtrTy}, false),
      GlobalValue::InternalLinkage, Name, M);
  addRuntimeAttrs(*F);

  auto Arg = F->arg_begin();
  Value *GroupID = &*Arg++;
  Value *FuncIndex = &*Arg++;
  Value *FuncPtr = &*Arg;
  IRBuilder<> IRB(BasicBlock::Create(Ctx, "entry", F));
  Value *GroupPtr = IRB.CreateInBoundsGEP(
      GroupsTy, Groups, {IRB.getInt32(0), GroupID}, "vllvm.icall.group");
  Value *ArrayField = IRB.CreateStructGEP(
      GroupTy, GroupPtr, 1, "vllvm.icall.func_array.field");
  Value *FuncArray =
      IRB.CreateLoad(PtrTy, ArrayField, "vllvm.icall.func_array");
  Value *Slot = IRB.CreateInBoundsGEP(PtrTy, FuncArray, FuncIndex,
                                      "vllvm.icall.func_slot");
  StoreInst *Store = IRB.CreateStore(FuncPtr, Slot);
  Store->setVolatile(true);
  IRB.CreateRetVoid();
  return F;
}

Function *createCallFunc(Module &M, StringRef Name, GlobalVariable *PoolData) {
  LLVMContext &Ctx = M.getContext();
  Type *I32Ty = Type::getInt32Ty(Ctx);
  Function *F = Function::Create(
      FunctionType::get(Type::getVoidTy(Ctx), {I32Ty}, false),
      GlobalValue::InternalLinkage, Name, M);
  F->setCallingConv(CallingConv::ICall);
  F->addParamAttr(0, Attribute::Nest);
  addRuntimeAttrs(*F);
  F->addFnAttr(Attribute::Naked);

  std::string PoolSymbol = PoolData->getName().str();
  Triple TT(M.getTargetTriple());
  std::string Asm;
  if (TT.isOSBinFormatMachO()) {
    Asm = "ubfx w16, w19, #16, #8\n"
          "and w17, w19, #0xff\n"
          "adrp x9, _" +
          PoolSymbol +
          "@PAGE\n"
          "ldr x9, [x9, _" +
          PoolSymbol +
          "@PAGEOFF]\n"
          "add x9, x9, x16, lsl #4\n"
          "ldr x9, [x9, #8]\n"
          "ldr x19, [x9, x17, lsl #3]\n"
          "ret x19\n";
  } else {
    Asm = "ubfx w16, w19, #16, #8\n"
          "and w17, w19, #0xff\n"
          "adrp x9, " +
          PoolSymbol +
          "\n"
          "ldr x9, [x9, :lo12:" +
          PoolSymbol +
          "]\n"
          "add x9, x9, x16, lsl #4\n"
          "ldr x9, [x9, #8]\n"
          "ldr x19, [x9, x17, lsl #3]\n"
          "ret x19\n";
  }

  IRBuilder<> IRB(BasicBlock::Create(Ctx, "entry", F));
  FunctionType *AsmTy = FunctionType::get(Type::getVoidTy(Ctx), false);
  InlineAsm *Thunk = InlineAsm::get(AsmTy, Asm, "", true);
  IRB.CreateCall(AsmTy, Thunk);
  IRB.CreateUnreachable();
  return F;
}

ICallRuntime createRuntime(Module &M, uint32_t GroupCount,
                           uint32_t GroupLength, StringRef Suffix) {
  LLVMContext &Ctx = M.getContext();
  Type *I32Ty = Type::getInt32Ty(Ctx);
  Type *PtrTy = PointerType::getUnqual(Ctx);
  StructType *GroupTy = StructType::get(Ctx, {I32Ty, PtrTy});
  ArrayType *StorageTy =
      ArrayType::get(PtrTy, uint64_t(GroupCount) * GroupLength);
  ArrayType *GroupsTy = ArrayType::get(GroupTy, GroupCount);

  std::string Prefix = (Twine("__vllvm_icall.") + Suffix).str();
  auto *Storage = new GlobalVariable(
      M, StorageTy, false, GlobalValue::InternalLinkage,
      ConstantAggregateZero::get(StorageTy), Prefix + ".func_storage");

  SmallVector<Constant *, 16> GroupValues;
  Constant *Zero = ConstantInt::get(I32Ty, 0);
  for (uint32_t Group = 0; Group < GroupCount; ++Group) {
    Constant *Offset = ConstantInt::get(I32Ty, Group * GroupLength);
    Constant *StorageIndexes[] = {Zero, Offset};
    Constant *ArrayPtr = ConstantExpr::getInBoundsGetElementPtr(
        StorageTy, Storage, StorageIndexes);
    GroupValues.push_back(ConstantStruct::get(
        GroupTy, {ConstantInt::get(I32Ty, GroupLength), ArrayPtr}));
  }
  auto *Groups = new GlobalVariable(
      M, GroupsTy, true, GlobalValue::InternalLinkage,
      ConstantArray::get(GroupsTy, GroupValues), Prefix + ".groups");
  auto *PoolData = new GlobalVariable(
      M, PtrTy, true, GlobalValue::InternalLinkage, Groups,
      Prefix + ".func_pool_data");
  appendToCompilerUsed(M, {PoolData});

  auto *GroupCountState = new GlobalVariable(
      M, I32Ty, false, GlobalValue::InternalLinkage,
      ConstantInt::get(I32Ty, 0), Prefix + ".group_count");
  auto *GroupLengthState = new GlobalVariable(
      M, I32Ty, false, GlobalValue::InternalLinkage,
      ConstantInt::get(I32Ty, 0), Prefix + ".group_length");

  ICallRuntime Runtime;
  Runtime.CreatePool = createCreatePoolFunction(
      M, Prefix + ".create_func_pool", GroupCountState, GroupLengthState);
  Runtime.RegisterFunc = createRegisterFunction(
      M, Prefix + ".register_func", GroupTy, GroupsTy, Groups);
  Runtime.CallFunc =
      createCallFunc(M, Prefix + ".call_func", PoolData);
  return Runtime;
}

void createRegistrationCtor(Module &M, const ICallRuntime &Runtime,
                            ArrayRef<Function *> Callees,
                            ArrayRef<PoolLocation> Locations,
                            uint32_t GroupCount, uint32_t GroupLength,
                            std::default_random_engine &Engine,
                            StringRef Suffix) {
  LLVMContext &Ctx = M.getContext();
  Function *Init = Function::Create(
      FunctionType::get(Type::getVoidTy(Ctx), false),
      GlobalValue::InternalLinkage,
      (Twine("__vllvm_icall.") + Suffix + ".register_funcs").str(), M);
  addRuntimeAttrs(*Init);

  IRBuilder<> IRB(BasicBlock::Create(Ctx, "entry", Init));
  IRB.CreateCall(Runtime.CreatePool,
                 {IRB.getInt32(GroupCount), IRB.getInt32(GroupLength)});

  std::vector<unsigned> Order(Callees.size());
  std::iota(Order.begin(), Order.end(), 0);
  std::shuffle(Order.begin(), Order.end(), Engine);
  for (unsigned GlobalIndex : Order) {
    const PoolLocation &Location = Locations[GlobalIndex];
    IRB.CreateCall(Runtime.RegisterFunc,
                   {IRB.getInt32(Location.Group),
                    IRB.getInt32(Location.Index), Callees[GlobalIndex]});
  }
  IRB.CreateRetVoid();
  // 注册必须早于用户构造函数，避免构造阶段首次调用时命中空槽。
  appendToGlobalCtors(M, Init, 0);
}

std::vector<uint32_t>
buildIndexArray(const FunctionPlan &Plan,
                const DenseMap<Function *, unsigned> &Globals,
                ArrayRef<PoolLocation> Locations) {
  std::vector<uint32_t> Values;
  Values.reserve(Plan.LocalCallees.size());
  for (Function *Callee : Plan.LocalCallees) {
    auto It = Globals.find(Callee);
    assert(It != Globals.end() && "missing global icall callee index");
    Values.push_back(Locations[It->second].pack());
  }
  return Values;
}

AttributeList createICallAttrs(CallInst &OldCall) {
  LLVMContext &Ctx = OldCall.getContext();
  AttributeList OldAttrs = OldCall.getAttributes();
  SmallVector<AttributeSet, 8> ParamAttrs;
  for (unsigned I = 0; I < OldCall.arg_size(); ++I)
    ParamAttrs.push_back(OldAttrs.getParamAttrs(I));
  ParamAttrs.push_back(AttributeSet::get(
      Ctx, {Attribute::get(Ctx, Attribute::Nest)}));
  // 函数级 memory 等属性描述的是原目标，不适用于查表 trampoline。
  return AttributeList::get(Ctx, AttributeSet(), OldAttrs.getRetAttrs(),
                            ParamAttrs);
}

void rewriteCall(const ICallRuntime &Runtime, uint32_t PackedIndex,
                 PlannedCall &Plan) {
  CallInst *OldCall = Plan.Call;
  SmallVector<Value *, 8> Args(OldCall->args());
  Args.push_back(ConstantInt::get(Type::getInt32Ty(OldCall->getContext()),
                                  PackedIndex));
  SmallVector<Type *, 8> ParamTypes;
  for (Value *Arg : Args)
    ParamTypes.push_back(Arg->getType());
  FunctionType *ICallTy = FunctionType::get(
      OldCall->getType(), ParamTypes, false);
  CallInst *NewCall = CallInst::Create(
      ICallTy, Runtime.CallFunc, Args, OldCall->getName(),
      OldCall->getIterator());
  NewCall->setCallingConv(CallingConv::ICall);
  NewCall->setAttributes(createICallAttrs(*OldCall));
  NewCall->setDebugLoc(OldCall->getDebugLoc());
  NewCall->copyMetadata(*OldCall);

  if (!OldCall->getType()->isVoidTy())
    OldCall->replaceAllUsesWith(NewCall);
  OldCall->eraseFromParent();
}
} // namespace

PreservedAnalyses IndirectCallPass::run(Module &M,
                                        ModuleAnalysisManager &MAM) {
  (void)MAM;
  Triple TT(M.getTargetTriple());
  if (TT.getArch() != Triple::aarch64 && TT.getArch() != Triple::aarch64_be)
    return PreservedAnalyses::all();

  std::vector<FunctionPlan> Plans;
  std::vector<Function *> GlobalCallees;
  DenseMap<Function *, unsigned> GlobalIndexes;
  collectPlans(M, Plans, GlobalCallees, GlobalIndexes);
  if (Plans.empty())
    return PreservedAnalyses::all();
  if (GlobalCallees.size() > MaxPoolFunctions) {
    M.getContext().emitError(
        "vllvm icall supports at most 65536 internal callees per module");
    return PreservedAnalyses::all();
  }

  std::unique_ptr<RandomNumberGenerator> RNG = M.createRNG("vllvm.icall");
  std::default_random_engine Engine(static_cast<unsigned>((*RNG)()));
  auto [GroupCount, GroupLength] =
      choosePoolShape(GlobalCallees.size(), Engine);
  std::vector<PoolLocation> Locations = placeCallees(
      GlobalCallees.size(), GroupCount, GroupLength, Engine);
  std::string Suffix = utohexstr((*RNG)());
  ICallRuntime Runtime =
      createRuntime(M, GroupCount, GroupLength, Suffix);
  createRegistrationCtor(M, Runtime, GlobalCallees, Locations, GroupCount,
                         GroupLength, Engine, Suffix);

  for (FunctionPlan &Plan : Plans) {
    errs() << "[vllvm] IndirectCallPass:" << Plan.Source->getName() << "\n";
    // index array 只在编译期存在；调用点直接把 packed index 常量送入 w19。
    std::vector<uint32_t> IndexArray =
        buildIndexArray(Plan, GlobalIndexes, Locations);
    for (PlannedCall &Call : Plan.Calls)
      rewriteCall(Runtime, IndexArray[Call.LocalIndex], Call);
    Plan.Source->addFnAttr(AppliedAttr);
  }
  return PreservedAnalyses::none();
}
