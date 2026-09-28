#include "EncryptoStrPass.h"
#include "EnstrPoolRuntimeEmbed.h"
#include "Utils.h"
#include "VLLVMAttribute.h"
#include "config/VLLVMConfig.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Bitcode/BitcodeReader.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/Linker/Linker.h"
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

// 字符串池：模块内全部密文解密到一块匿名内存，统一按下标访问。
// 明文只存在于匿名页（不在堆、不在可静态反汇编的数据段），初始化用
// 原子 CAS 抢锁，并发首次访问也只分配一次。池逻辑（mmap/CAS/解密/
// 下标访问）由 src/c_func/enstrpool/enstrpool.c 以位码链入，Android/ELF
// 优先；这里只负责链接运行时并生成描述表。

struct PoolMember {
  GlobalVariable *GV = nullptr;
  const std::array<uint8_t, KeySize> *Key = nullptr;
  uint64_t Offset = 0;
};

// 链接 enstr 池运行时位码，返回 __vllvm_enstr_get 访问器；失败返回
// nullptr（此时回退为不改写字符串）。
Function *linkEnstrPoolRuntime(Module &M) {
  ArrayRef<std::uint8_t> Bytes = llvm::vllvm::getEnstrPoolRuntimeBitcode();
  MemoryBufferRef Buffer(
      StringRef(reinterpret_cast<const char *>(Bytes.data()), Bytes.size()),
      "enstr-pool.bc");
  Expected<std::unique_ptr<Module>> Parsed =
      parseBitcodeFile(Buffer, M.getContext());
  if (!Parsed)
    return nullptr;
  std::unique_ptr<Module> Runtime = std::move(*Parsed);
  Runtime->setTargetTriple(M.getTargetTriple());
  Runtime->setDataLayout(M.getDataLayout());
  // 嵌入位码的宿主目标属性不属于目标模块，统一剥掉。
  for (Function &F : *Runtime) {
    F.removeFnAttr("target-cpu");
    F.removeFnAttr("target-features");
    F.removeFnAttr("tune-cpu");
  }
  if (Linker(M).linkInModule(std::move(Runtime)))
    return nullptr;
  Function *Get = M.getFunction("__vllvm_enstr_get");
  if (!Get || Get->isDeclaration())
    return nullptr;
  Get->setLinkage(GlobalValue::InternalLinkage);
  return Get;
}

// 生成描述表并填充运行时声明的链接期常量。全部保持 internal：
// 多 TU 各自持池互不冲突，也不给分析侧留符号入口。
bool materializePoolTable(Module &M, ArrayRef<PoolMember> Members,
                          uint64_t TotalSize) {
  LLVMContext &Ctx = M.getContext();
  IntegerType *I64Ty = Type::getInt64Ty(Ctx);
  StructType *DescTy =
      StructType::getTypeByName(Ctx, "struct.vllvm_enstr_desc");
  if (!DescTy)
    return false;

  SmallVector<Constant *, 16> Elems;
  for (const PoolMember &Member : Members) {
    auto *ArrayTy = cast<ArrayType>(Member.GV->getValueType());
    uint64_t Size = ArrayTy->getNumElements();
    // 16 字节密钥按小端打包进 key0/key1，与运行时的展开方式一致。
    uint64_t Key0, Key1;
    memcpy(&Key0, Member.Key->data(), 8);
    memcpy(&Key1, Member.Key->data() + 8, 8);
    Elems.push_back(ConstantStruct::get(
        DescTy, {Member.GV, ConstantInt::get(I64Ty, Member.Offset),
                 ConstantInt::get(I64Ty, Size), ConstantInt::get(I64Ty, Key0),
                 ConstantInt::get(I64Ty, Key1)}));
  }

  ArrayType *TableTy = ArrayType::get(DescTy, Elems.size());
  auto *Table = new GlobalVariable(
      M, TableTy, true, GlobalValue::PrivateLinkage,
      ConstantArray::get(TableTy, Elems), "vllvm.enstr.desc.table");
  Table->setUnnamedAddr(GlobalValue::UnnamedAddr::Global);

  // 运行时里的 extern 声明在此补上定义（getOrInsertGlobal 复用声明）。
  auto *TablePtr = cast<GlobalVariable>(M.getOrInsertGlobal(
      "__vllvm_enstr_table", PointerType::getUnqual(Ctx)));
  Constant *Zero32 = ConstantInt::get(Type::getInt32Ty(Ctx), 0);
  Constant *TableIndexes[] = {Zero32, Zero32};
  TablePtr->setInitializer(ConstantExpr::getInBoundsGetElementPtr(
      TableTy, Table, TableIndexes));
  TablePtr->setConstant(true);
  TablePtr->setLinkage(GlobalValue::InternalLinkage);

  auto DefineConst = [&](StringRef Name, Type *Ty, Constant *Value) {
    auto *GV = cast<GlobalVariable>(M.getOrInsertGlobal(Name, Ty));
    GV->setInitializer(Value);
    GV->setConstant(true);
    GV->setLinkage(GlobalValue::InternalLinkage);
  };
  DefineConst("__vllvm_enstr_count", I64Ty,
              ConstantInt::get(I64Ty, Members.size()));
  DefineConst("__vllvm_enstr_pool_size", I64Ty,
              ConstantInt::get(I64Ty, TotalSize));

  // MAP_PRIVATE|MAP_ANON：Android/ELF 0x22，Darwin 0x1002；PROT_RW、fd
  // 固定在运行时里。
  Triple TT(M.getTargetTriple());
  uint32_t Flags = TT.isOSBinFormatMachO() ? 0x1002 : 0x22;
  DefineConst("__vllvm_enstr_mmap_flags", Type::getInt32Ty(Ctx),
              ConstantInt::get(Type::getInt32Ty(Ctx), Flags));
  return true;
}

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
    // 主字符串依次入池；指针别名（isDouble）与主字符串共用 strID，
    // 沿用同一份明文与下标。
    uint64_t TotalSize = 0;
    std::map<uint64_t, uint64_t> OffsetOfID;
    for (EncryptoStr *encryptoStr : encryptoStrPool) {
      if (encryptoStr->isDouble)
        continue;
      encryptoStr->poolOffset = TotalSize;
      TotalSize += (cast<ArrayType>(encryptoStr->strVar->getValueType())
                        ->getNumElements() +
                    PoolSlotAlign - 1) &
                   ~(PoolSlotAlign - 1);
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

    // 先链运行时再生成描述表（表类型来自链入的 struct），失败则整段
    // 回退：不加密任何字符串，保持语义完整。
    Function *Accessor = linkEnstrPoolRuntime(M);
    if (!Accessor || !materializePoolTable(M, Members, TotalSize)) {
      M.getContext().emitError("vllvm enstr: failed to link pool runtime");
      return PreservedAnalyses::all();
    }

    for (EncryptoStr *encryptoStr : encryptoStrPool)
      if (!encryptoStr->insertPoolAccess(Accessor)) {
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
