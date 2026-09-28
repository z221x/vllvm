#include "EncryptoStrPass.h"
#include "Utils.h"
#include "VLLVMAttribute.h"
#include "config/VLLVMConfig.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/Support/RandomNumberGenerator.h"
#include "llvm/TargetParser/Triple.h"

#include <array>
#include <cstring>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

using namespace llvm;

namespace {
constexpr size_t KeySize = 16;
// 池内字符串 16 字节对齐，避免跨缓存行并让相邻下标带有随机间隙。
constexpr uint64_t PoolSlotAlign = 16;
// level 2 常量加密的最小幅度：更小的常量保留原样，控制代码膨胀。
constexpr int64_t ConstEncryptThreshold = 256;

bool reachesGlobalAnnotations(User *U, SmallPtrSetImpl<User *> &Visited) {
  if (!U || !Visited.insert(U).second)
    return false;
  if (auto *GV = dyn_cast<GlobalVariable>(U))
    return GV->getName() == "llvm.global.annotations";

  for (User *Next : U->users())
    if (reachesGlobalAnnotations(Next, Visited))
      return true;
  return false;
}

bool isGlobalAnnotationUser(User *U) {
  SmallPtrSet<User *, 8> Visited;
  return reachesGlobalAnnotations(U, Visited);
}

bool isMarkedStringTarget(GlobalVariable &StringGV) {
  if (llvm::vllvm::hasVLLVMStringEncryptionAnnotation(StringGV))
    return true;

  for (User *UserFirst : StringGV.users()) {
    auto *GV = dyn_cast<GlobalVariable>(UserFirst);
    if (GV && llvm::vllvm::hasVLLVMStringEncryptionAnnotation(*GV))
      return true;
  }
  return false;
}
} // namespace

class EncryptoStrPass::EncryptoStr {
public:
  uint64_t strID; // 用于标识以及密钥生成
  GlobalVariable *strVar;
  std::array<uint8_t, KeySize> encKey{};
  std::vector<User *> callUser;
  // 明文在匿名池内的字节下标；由 StringPool 统一分配。
  uint64_t poolOffset = 0;
  Module &M;
  bool isDouble;

  EncryptoStr(uint64_t id, GlobalVariable *strvar, bool isdouble, Module &m)
      : strID(id), strVar(strvar), M(m), isDouble(isdouble) {
    // 生成key
    getRandomKey();
  }

  void encryptoStr() {
    auto *cda = dyn_cast<ConstantDataArray>(strVar->getInitializer());
    if (!cda)
      return;

    std::string encValue = encrypto(cda->getAsString());
    // 修改构造器
    Constant *newInit = ConstantDataArray::getString(
        M.getContext(), StringRef(encValue.data(), encValue.size()), false);
    strVar->setInitializer(newInit);
    // 设置可写
    strVar->setConstant(false);
  }

  std::string encrypto(StringRef strRef) const {
    std::string result(strRef.begin(), strRef.end());
    for (size_t i = 0; i < result.size(); ++i) {
      result[i] = static_cast<char>(
          static_cast<uint8_t>(result[i]) ^ encKey[i % encKey.size()]);
    }
    return result;
  }

  void getRandomKey() {
    // 根据strID生成唯一的RandomNumberGenerator
    std::unique_ptr<RandomNumberGenerator> RNG =
        M.createRNG(std::to_string(strID));
    uint64_t part1 = (*RNG)(); // 前8字节
    uint64_t part2 = (*RNG)(); // 后8字节
    memcpy(encKey.data(), &part1, 8);
    memcpy(encKey.data() + 8, &part2, 8);
  }

  // 用下标访问替换原字符串地址：分析侧只能看到匿名池基址 + 偏移，
  // 不再出现明文的绝对地址。
  bool insertPoolAccess(Function *Accessor) {
    if (!Accessor)
      return false;

    auto createDecryptedValue = [&](Instruction *Before) -> Value * {
      IRBuilder<> IRB(Before);
      Value *Plain = fixEH(IRB.CreateCall(
          Accessor, {IRB.getInt64(static_cast<uint64_t>(poolOffset))}));
      if (!isDouble)
        return Plain;
      Value *Slot = IRB.CreateAlloca(IRB.getPtrTy(), nullptr);
      IRB.CreateStore(Plain, Slot);
      return Slot;
    };

    for (User *user : callUser) {
      auto *instr = dyn_cast<Instruction>(user);
      if (!instr)
        return false;

      instr->replaceUsesOfWith(strVar, createDecryptedValue(instr));
    }
    return true;
  }
};

namespace {

// 匿名内存申请按目标二进制格式选择：MachO/ELF 用 mmap，COFF 用
// VirtualAlloc；失败哨兵分别是 MAP_FAILED(-1) 与 NULL。
std::pair<CallInst *, Constant *>
emitAnonymousAlloc(IRBuilder<> &B, Module &M, Value *Size) {
  LLVMContext &Ctx = M.getContext();
  const DataLayout &DL = M.getDataLayout();
  IntegerType *SizeTy = DL.getIntPtrType(Ctx);
  PointerType *PtrTy = B.getPtrTy();
  Triple TT(M.getTargetTriple());

  if (TT.isOSBinFormatCOFF()) {
    FunctionType *Ty = FunctionType::get(
        PtrTy, {PtrTy, SizeTy, B.getInt32Ty(), B.getInt32Ty()}, false);
    FunctionCallee F = M.getOrInsertFunction("VirtualAlloc", Ty);
    // MEM_COMMIT|MEM_RESERVE=0x3000，PAGE_READWRITE=4。
    CallInst *Call =
        B.CreateCall(F, {ConstantPointerNull::get(PtrTy), Size,
                         B.getInt32(0x3000), B.getInt32(4)});
    return {Call, ConstantPointerNull::get(PtrTy)};
  }

  FunctionType *Ty = FunctionType::get(
      PtrTy, {PtrTy, SizeTy, B.getInt32Ty(), B.getInt32Ty(), B.getInt32Ty(),
              SizeTy},
      false);
  FunctionCallee F = M.getOrInsertFunction("mmap", Ty);
  // MAP_PRIVATE=0x2；MAP_ANON：Darwin 0x1000，Linux/ELF 0x20。
  uint32_t Flags = TT.isOSDarwin() ? 0x1002 : 0x22;
  CallInst *Call = B.CreateCall(
      F, {ConstantPointerNull::get(PtrTy), Size, B.getInt32(3),
          B.getInt32(Flags), B.getInt32(-1), ConstantInt::get(SizeTy, 0)});
  Constant *MapFailed =
      ConstantExpr::getIntToPtr(ConstantInt::get(SizeTy, -1ULL), PtrTy);
  return {Call, MapFailed};
}

// 字符串池：模块内全部密文解密到一块匿名内存，统一按下标访问。
// 明文只存在于匿名页（不在堆、不在可静态反汇编的数据段），初始化用
// 原子 CAS 抢锁，并发首次访问也只分配一次。
struct PoolMember {
  GlobalVariable *GV = nullptr;
  const std::array<uint8_t, KeySize> *Key = nullptr;
  uint64_t Offset = 0;
};

class StringPool {
public:
  StringPool(Module &M) : M(M), DL(M.getDataLayout()) {}

  uint64_t addString(uint64_t Size) {
    uint64_t Offset = TotalSize;
    TotalSize += (Size + PoolSlotAlign - 1) & ~(PoolSlotAlign - 1);
    return Offset;
  }

  void finalize(ArrayRef<PoolMember> Members) {
    assert(!Members.empty() && "string pool needs at least one member");

    LLVMContext &Ctx = M.getContext();
    IRBuilder<> IRB(Ctx);
    IntegerType *I64Ty = Type::getInt64Ty(Ctx);
    PointerType *PtrTy = IRB.getPtrTy();
    IntegerType *I8Ty = IRB.getInt8Ty();
    Align PtrAlign = DL.getABITypeAlign(PtrTy);
    Constant *Null = ConstantPointerNull::get(PtrTy);
    // 1 表示初始化中，不会作为字符串地址发布。
    Constant *Busy =
        ConstantExpr::getIntToPtr(ConstantInt::get(I64Ty, 1), PtrTy);

    Base = new GlobalVariable(M, PtrTy, false, GlobalValue::PrivateLinkage,
                              Null, "__vllvm_enstr.pool.base");
    Base->setAlignment(PtrAlign);

    Init = Function::Create(FunctionType::get(Type::getVoidTy(Ctx), false),
                            Function::PrivateLinkage, "__vllvm_enstr.init", &M);
    Get = Function::Create(FunctionType::get(PtrTy, {I64Ty}, false),
                           Function::PrivateLinkage, "__vllvm_enstr.get", &M);

    BasicBlock *InitEntryBB = BasicBlock::Create(Ctx, "entry", Init);
    BasicBlock *CheckBB = BasicBlock::Create(Ctx, "pool.check", Init);
    BasicBlock *ReadyBB = BasicBlock::Create(Ctx, "pool.ready", Init);
    BasicBlock *DoneBB = BasicBlock::Create(Ctx, "pool.done", Init);
    BasicBlock *ClaimBB = BasicBlock::Create(Ctx, "pool.claim", Init);
    BasicBlock *AllocBB = BasicBlock::Create(Ctx, "pool.allocate", Init);
    BasicBlock *FailedBB = BasicBlock::Create(Ctx, "pool.failed", Init);

    // 自旋回边指向 pool.check，入口块必须保持无前驱。
    IRB.SetInsertPoint(InitEntryBB);
    IRB.CreateBr(CheckBB);

    IRB.SetInsertPoint(CheckBB);
    LoadInst *Cached =
        IRB.CreateAlignedLoad(PtrTy, Base, PtrAlign, "pool.base");
    Cached->setAtomic(AtomicOrdering::Acquire);
    IRB.CreateCondBr(IRB.CreateICmpEQ(Cached, Null), ClaimBB, ReadyBB);

    IRB.SetInsertPoint(ReadyBB);
    // 自旋等待初始化中的池发布，避免把 Busy 哨兵当字符串地址用。
    IRB.CreateCondBr(IRB.CreateICmpEQ(Cached, Busy), CheckBB, DoneBB);

    IRB.SetInsertPoint(DoneBB);
    IRB.CreateRetVoid();

    IRB.SetInsertPoint(ClaimBB);
    AtomicCmpXchgInst *Claim = IRB.CreateAtomicCmpXchg(
        Base, Null, Busy, PtrAlign, AtomicOrdering::AcquireRelease,
        AtomicOrdering::Acquire);
    IRB.CreateCondBr(IRB.CreateExtractValue(Claim, 1), AllocBB, CheckBB);

    IRB.SetInsertPoint(AllocBB);
    // 申请大小按目标指针宽度（i386 是 i32），与 mmap/VirtualAlloc 声明一致。
    auto [Alloc, Failure] = emitAnonymousAlloc(
        IRB, M,
        ConstantInt::get(DL.getIntPtrType(Ctx), TotalSize));
    Value *Region = Alloc;
    // 失败比较在此算好；分支在接入第一个解密块时发出。
    Value *AllocFailed = IRB.CreateICmpEQ(Region, Failure);
    BasicBlock *Cursor = AllocBB;

    IRB.SetInsertPoint(FailedBB);
    // 分配失败不能发布哨兵指针，也不能让其他线程永久等待初始化。
    IRB.CreateCall(Intrinsic::getOrInsertDeclaration(&M, Intrinsic::trap));
    IRB.CreateUnreachable();

    ArrayType *KeyArrayType = ArrayType::get(I8Ty, KeySize);

    // 逐个字符串解密：region[offset + i] = str[i] ^ key[i % 16]。
    // key/游标 alloca 放在循环前置块，避免循环内反复扩张栈。首块的
    // 占位 cond-br 需要先移除；后续块的 NextBB 本就是空块。
    for (const PoolMember &Member : Members) {
      BasicBlock *LoopBB = BasicBlock::Create(Ctx, "pool.decrypt", Init);
      BasicBlock *BodyBB =
          BasicBlock::Create(Ctx, "pool.decrypt.body", Init);
      BasicBlock *NextBB = BasicBlock::Create(Ctx, "pool.decrypt.next", Init);

      if (Instruction *Term = Cursor->getTerminator())
        Term->eraseFromParent();
      IRB.SetInsertPoint(Cursor);
      Value *KeyVar = IRB.CreateAlloca(KeyArrayType, nullptr, "key");
      for (size_t I = 0; I < Member.Key->size(); ++I) {
        Value *KeyElemPtr =
            IRB.CreateGEP(KeyArrayType, KeyVar,
                          {IRB.getInt32(0), IRB.getInt32(static_cast<uint32_t>(I))});
        IRB.CreateStore(IRB.getInt8((*Member.Key)[I]), KeyElemPtr);
      }
      Value *IVar = IRB.CreateAlloca(I64Ty, nullptr, "i");
      IRB.CreateStore(IRB.getInt64(0), IVar);
      // 首块（allocate）保留分配失败检查；后续块的 NextBB 是空块。
      if (Cursor == AllocBB)
        IRB.CreateCondBr(AllocFailed, FailedBB, LoopBB);
      else
        IRB.CreateBr(LoopBB);

      auto *ArrayTy = cast<ArrayType>(Member.GV->getValueType());
      uint64_t StrSize = ArrayTy->getNumElements();

      IRB.SetInsertPoint(LoopBB);
      Value *ILoad = IRB.CreateLoad(I64Ty, IVar, "iLoad");
      IRB.CreateCondBr(
          IRB.CreateICmpULT(ILoad, IRB.getInt64(StrSize), "cond"), BodyBB,
          NextBB);

      IRB.SetInsertPoint(BodyBB);
      Value *StrPtr =
          IRB.CreateGEP(I8Ty, Member.GV, ILoad, "strPtr");
      Value *StrLoad = IRB.CreateLoad(I8Ty, StrPtr, "strLoad");
      Value *KeyOffset =
          IRB.CreateURem(ILoad, IRB.getInt64(Member.Key->size()), "keyOffset");
      Value *KeyPtr = IRB.CreateInBoundsGEP(
          KeyArrayType, KeyVar, {IRB.getInt32(0), KeyOffset}, "keyPtr");
      Value *KeyLoad = IRB.CreateLoad(I8Ty, KeyPtr, "keyLoad");
      Value *XorValue = IRB.CreateXor(StrLoad, KeyLoad, "xorValue");
      Value *OutPtr = IRB.CreateGEP(
          I8Ty, Region,
          IRB.CreateAdd(ILoad, IRB.getInt64(Member.Offset), "poolIdx"),
          "outPtr");
      IRB.CreateStore(XorValue, OutPtr);
      IRB.CreateStore(IRB.CreateAdd(ILoad, IRB.getInt64(1), "iNext"), IVar);
      IRB.CreateBr(LoopBB);

      Cursor = NextBB;
    }

    // 解密完成后才发布基地址；明文保持进程生命周期，不主动释放。
    // 最后一个成员的 NextBB 此时为空块，直接作为发布块。
    IRB.SetInsertPoint(Cursor);
    StoreInst *Publish = IRB.CreateAlignedStore(Region, Base, PtrAlign);
    Publish->setAtomic(AtomicOrdering::Release);
    IRB.CreateRetVoid();

    // 访问器：传入池内下标，惰性初始化后返回 基址+下标。初始化期间
    // base 是 BUSY 哨兵，必须自旋等发布，不能把哨兵当基址做 GEP。
    BasicBlock *EntryBB = BasicBlock::Create(Ctx, "entry", Get);
    BasicBlock *CheckBusyBB = BasicBlock::Create(Ctx, "check.busy", Get);
    BasicBlock *InitBB = BasicBlock::Create(Ctx, "init", Get);
    BasicBlock *WaitBB = BasicBlock::Create(Ctx, "wait", Get);
    BasicBlock *CalcBB = BasicBlock::Create(Ctx, "calc", Get);
    auto Arg = Get->arg_begin();
    Value *IndexArg = Arg++;
    IndexArg->setName("index");

    IRB.SetInsertPoint(EntryBB);
    LoadInst *Base0 = IRB.CreateAlignedLoad(PtrTy, Base, PtrAlign, "pool.base");
    Base0->setAtomic(AtomicOrdering::Acquire);
    IRB.CreateCondBr(IRB.CreateICmpEQ(Base0, Null), InitBB, CheckBusyBB);

    IRB.SetInsertPoint(CheckBusyBB);
    IRB.CreateCondBr(IRB.CreateICmpEQ(Base0, Busy), WaitBB, CalcBB);

    IRB.SetInsertPoint(InitBB);
    IRB.CreateCall(Init);
    IRB.CreateBr(CalcBB);

    IRB.SetInsertPoint(WaitBB);
    LoadInst *WaitBase =
        IRB.CreateAlignedLoad(PtrTy, Base, PtrAlign, "pool.base");
    WaitBase->setAtomic(AtomicOrdering::Acquire);
    IRB.CreateCondBr(IRB.CreateICmpEQ(WaitBase, Busy), WaitBB, CalcBB);

    IRB.SetInsertPoint(CalcBB);
    LoadInst *Base1 = IRB.CreateAlignedLoad(PtrTy, Base, PtrAlign, "pool.base");
    Base1->setAtomic(AtomicOrdering::Acquire);
    IRB.CreateRet(IRB.CreateGEP(I8Ty, Base1, IndexArg, "strPtr"));
  }

  Function *accessor() const { return Get; }

private:
  Module &M;
  const DataLayout &DL;
  uint64_t TotalSize = 0;
  GlobalVariable *Base = nullptr;
  Function *Init = nullptr;
  Function *Get = nullptr;
};

// level 2：常量加密。把作用域内函数的标量整数常量换成
// trunc(volatile load @vllvm.enstr.const.table[i] ^ K)；表里存的是密文，
// 分析侧看不到原始立即数。
bool encryptConstants(Module &M) {
  bool EncryptAllStrings = llvm::vllvm::moduleRequestsFunctionStringEncryption(M);
  auto InScope = [&](Function &F) {
    return !F.isDeclaration() && !F.empty() &&
           (EncryptAllStrings ||
            llvm::vllvm::getFunctionVLLVMOptions(F).EncryptoStr);
  };

  std::map<int64_t, unsigned> SlotOf;
  SmallVector<int64_t, 16> SlotValues;
  SmallVector<uint64_t, 16> SlotKeys;
  std::unique_ptr<RandomNumberGenerator> RNG =
      M.createRNG("vllvm.enstr.const");

  struct Replacement {
    Instruction *I;
    unsigned Op;
    unsigned Slot;
  };
  SmallVector<Replacement, 32> Replacements;

  for (Function &F : M) {
    // 只改用户代码：VLLVM 生成的运行时自身保持立即数可见。
    if (!InScope(F) || F.getName().starts_with("__vllvm_") ||
        F.getName().starts_with("_decrypto"))
      continue;

    for (BasicBlock &BB : F) {
      for (Instruction &I : BB) {
        // 这些指令的立即数必须是常量，或替换会破坏布局语义。
        if (isa<PHINode>(I) || isa<SwitchInst>(I) || isa<AllocaInst>(I) ||
            isa<GetElementPtrInst>(I))
          continue;

        for (unsigned Op = 0, E = I.getNumOperands(); Op < E; ++Op) {
          auto *C = dyn_cast<ConstantInt>(I.getOperand(Op));
          if (!C || C->getType()->getIntegerBitWidth() < 8 ||
              C->getType()->getIntegerBitWidth() > 64)
            continue;
          int64_t Value = C->getSExtValue();
          if (Value > -ConstEncryptThreshold && Value < ConstEncryptThreshold)
            continue;

          auto [It, Inserted] = SlotOf.try_emplace(Value, SlotValues.size());
          if (Inserted) {
            SlotValues.push_back(Value);
            SlotKeys.push_back((*RNG)());
          }
          Replacements.push_back({&I, Op, It->second});
        }
      }
    }
  }

  if (Replacements.empty())
    return false;

  LLVMContext &Ctx = M.getContext();
  IntegerType *I64Ty = Type::getInt64Ty(Ctx);
  ArrayType *TableTy = ArrayType::get(I64Ty, SlotValues.size());
  SmallVector<Constant *, 16> Inits;
  for (size_t I = 0; I < SlotValues.size(); ++I)
    Inits.push_back(ConstantInt::get(
        I64Ty, static_cast<uint64_t>(SlotValues[I]) ^ SlotKeys[I]));
  auto *Table = new GlobalVariable(
      M, TableTy, true, GlobalValue::PrivateLinkage,
      ConstantArray::get(TableTy, Inits), "vllvm.enstr.const.table");
  Table->setUnnamedAddr(GlobalValue::UnnamedAddr::Global);

  for (const Replacement &R : Replacements) {
    auto *C = cast<ConstantInt>(R.I->getOperand(R.Op));
    IRBuilder<> IRB(R.I);
    Value *Elem = IRB.CreateInBoundsGEP(
        TableTy, Table, {IRB.getInt32(0), IRB.getInt32(R.Slot)},
        "vllvm.enstr.const.elem");
    LoadInst *Load = IRB.CreateLoad(I64Ty, Elem, "vllvm.enstr.const.load");
    Load->setVolatile(true);
    Value *Plain = IRB.CreateXor(Load, IRB.getInt64(SlotKeys[R.Slot]),
                                 "vllvm.enstr.const.mix");
    R.I->setOperand(R.Op, C->getType()->isIntegerTy(64)
                              ? Plain
                              : IRB.CreateTrunc(Plain, C->getType()));
  }
  return true;
}
} // namespace

PreservedAnalyses EncryptoStrPass::run(Module &M, ModuleAnalysisManager &MAM) {
  llvm::vllvm::VLLVMConfig &Config = llvm::vllvm::VLLVMConfig::get();
  // enstr 等级（Pass 自定语义）：1=加密字符串；2=再加密常量。
  Config.registerPassLevels("enstr", 1, 2);
  unsigned Level = Config.getLevel("enstr");
  errs() << "[vllvm] EncryptoStrPass:" << M.getName() << "\n";
  if (Level == 0)
    return PreservedAnalyses::all();
  bool isChanged = false;
  // 必须先降级再收集字符串 users，避免保存随后被删除的 PHI 指针。
  for (Function &F : M)
    isChanged |= lowerPHINodes(F) == PHILoweringResult::Lowered;
  std::vector<EncryptoStr *> encryptoStrPool = makeEncryptoStrPool(M);

  if (!encryptoStrPool.empty()) {
    StringPool Pool(M);
    // 主字符串依次入池；指针别名（isDouble）与主字符串共用 strID，
    // 沿用同一份明文与下标。
    std::map<uint64_t, uint64_t> OffsetOfID;
    for (EncryptoStr *encryptoStr : encryptoStrPool) {
      if (encryptoStr->isDouble)
        continue;
      encryptoStr->poolOffset = Pool.addString(
          cast<ArrayType>(encryptoStr->strVar->getValueType())
              ->getNumElements());
      OffsetOfID.try_emplace(encryptoStr->strID, encryptoStr->poolOffset);
    }
    for (EncryptoStr *encryptoStr : encryptoStrPool)
      if (encryptoStr->isDouble)
        encryptoStr->poolOffset = OffsetOfID[encryptoStr->strID];

    SmallVector<PoolMember, 16> Members;
    for (EncryptoStr *encryptoStr : encryptoStrPool) {
      if (encryptoStr->isDouble)
        continue;
      Members.push_back(
          {encryptoStr->strVar, &encryptoStr->encKey, encryptoStr->poolOffset});
    }
    Pool.finalize(Members);

    for (EncryptoStr *encryptoStr : encryptoStrPool)
      if (!encryptoStr->insertPoolAccess(Pool.accessor())) {
        // 收集阶段已过滤非常量用户，这里不可达。
        M.getContext().emitError("vllvm enstr: unsupported string use remained");
        return PreservedAnalyses::all();
      }
    for (EncryptoStr *encryptoStr : encryptoStrPool)
      encryptoStr->encryptoStr();
    isChanged = true;
  }

  // level >= 2：常量也进密文表。
  if (Level >= 2)
    isChanged |= encryptConstants(M);

  for (EncryptoStr *encryptoStr : encryptoStrPool)
    delete encryptoStr;
  return isChanged ? PreservedAnalyses::none() : PreservedAnalyses::all();
}

std::vector<EncryptoStrPass::EncryptoStr *>
EncryptoStrPass::makeEncryptoStrPool(Module &M) {
  std::vector<EncryptoStr *> encStringPool;
  bool EncryptAllStrings = llvm::vllvm::moduleRequestsFunctionStringEncryption(M);
  uint64_t ID = 0;
  for (GlobalVariable &globalVar : M.globals()) {
    ++ID;
    // 全局变量有构造器
    if (!globalVar.hasInitializer() || !globalVar.getValueType()->isArrayTy())
      continue;

    auto *cda = dyn_cast<ConstantDataArray>(globalVar.getInitializer());
    // 字符串
    if (!cda || !cda->isString())
      continue;

    // 函数级 enstr 保持旧行为：扫描整个 Module。
    // 变量级 enstr 只加密被标记的字符串变量，或被标记指针变量引用的字符串。
    if (!EncryptAllStrings && !isMarkedStringTarget(globalVar))
      continue;

    auto *encryptoStr = new EncryptoStr(ID, &globalVar, false, M);
    std::vector<EncryptoStr *> pendingDoubleStrings;
    bool supported = true;

    // 第一层调用
    for (User *userFirst : globalVar.users()) {
      if (isGlobalAnnotationUser(userFirst))
        continue;

      // 公共工具回退的 PHI 保持原样，对相关字符串也回退，不能在 PHI 前插调用。
      if (isa<PHINode>(userFirst)) {
        supported = false;
        break;
      }
      if (isa<Instruction>(userFirst)) {
        encryptoStr->callUser.push_back(userFirst);
        continue;
      }

      // 如果使用者是全局变量，则寻找使用者的调用
      auto *global = dyn_cast<GlobalVariable>(userFirst);
      if (!global) {
        supported = false;
        break;
      }

      auto *encryptoStrDouble = new EncryptoStr(ID, global, true, M);
      for (User *userSecond : global->users()) {
        if (isGlobalAnnotationUser(userSecond))
          continue;

        if (isa<PHINode>(userSecond)) {
          supported = false;
          break;
        }
        if (isa<Instruction>(userSecond)) {
          encryptoStrDouble->callUser.push_back(userSecond);
          continue;
        }

        // 如果有第三层引用就不在加密此字符串
        supported = false;
        break;
      }

      if (!supported) {
        delete encryptoStrDouble;
        break;
      }
      pendingDoubleStrings.push_back(encryptoStrDouble);
    }

    if (!supported) {
      for (EncryptoStr *pending : pendingDoubleStrings)
        delete pending;
      delete encryptoStr;
      continue;
    }

    // 没有任何真实使用者的字符串（含仅剩注解痕迹的元数据串）不进池：
    // 解密它没有意义，还会白白扩大匿名区。
    if (encryptoStr->callUser.empty() && pendingDoubleStrings.empty()) {
      delete encryptoStr;
      continue;
    }

    encStringPool.insert(encStringPool.end(), pendingDoubleStrings.begin(),
                         pendingDoubleStrings.end());
    encStringPool.push_back(encryptoStr);
  }
  return encStringPool;
}
