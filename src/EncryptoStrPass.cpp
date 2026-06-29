#include "EncryptoStrPass.h"
#include "VLLVMAttribute.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Instructions.h"
#include "llvm/Support/RandomNumberGenerator.h"

#include <array>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

using namespace llvm;

namespace {
constexpr size_t KeySize = 16;

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
}

class EncryptoStrPass::EncryptoStr {
public:
  uint64_t strID; // 用于标识以及密钥生成
  GlobalVariable *strVar;
  std::array<uint8_t, KeySize> encKey{};
  std::vector<User *> callUser;
  Function *decFunc;
  Module &M;
  bool isDouble;

  EncryptoStr(uint64_t id, GlobalVariable *strvar, bool isdouble, Module &m)
      : strID(id), strVar(strvar), decFunc(nullptr), M(m), isDouble(isdouble) {
    // 生成key
    getRandomKey();
    makeDecryptoFunc();
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

  void makeDecryptoFunc() {
    if (isDouble) {
      decFunc = M.getFunction("_decrypto" + std::to_string(strID));
      return;
    }

    LLVMContext &Ctx = M.getContext();
    IRBuilder<> IRB(Ctx);
    FunctionType *decryptoFuncType =
        FunctionType::get(IRB.getPtrTy(), {IRB.getPtrTy()}, false);
    decFunc = Function::Create(decryptoFuncType, Function::PrivateLinkage,
                               "_decrypto" + std::to_string(strID), &M);

    // 创建函数体
    BasicBlock *entryBB = BasicBlock::Create(Ctx, "entry", decFunc);
    BasicBlock *loopBB = BasicBlock::Create(Ctx, "loop", decFunc);
    BasicBlock *loopbrBB = BasicBlock::Create(Ctx, "loopbr", decFunc);
    BasicBlock *exitBB = BasicBlock::Create(Ctx, "exit", decFunc);

    // 获取参数
    auto argIter = decFunc->arg_begin();
    Value *strArg = argIter++;
    strArg->setName("strArg");

    IRB.SetInsertPoint(entryBB);
    auto *arrayTy = cast<ArrayType>(strVar->getValueType());
    uint64_t strSize = arrayTy->getNumElements();
    // 创建result
    FunctionType *mallocType =
        FunctionType::get(IRB.getPtrTy(), {IRB.getInt64Ty()}, false);
    FunctionCallee mallocFunc = M.getOrInsertFunction("malloc", mallocType);
    Value *result = IRB.CreateCall(mallocFunc, {IRB.getInt64(strSize)});

    // 创建变量key
    ArrayType *keyArrayType = ArrayType::get(IRB.getInt8Ty(), KeySize);
    Value *keyVar = IRB.CreateAlloca(keyArrayType, nullptr, "key");
    for (size_t i = 0; i < encKey.size(); ++i) {
      // 数组索引、元素索引
      Value *index[] = {IRB.getInt32(0), IRB.getInt32(static_cast<uint32_t>(i))};
      Value *keyElemPtr = IRB.CreateGEP(keyArrayType, keyVar, index);
      Value *keyValue = IRB.getInt8(encKey[i]);
      // 存储元素值
      IRB.CreateStore(keyValue, keyElemPtr);
    }

    // 创建变量i=0
    Value *iVar = IRB.CreateAlloca(IRB.getInt64Ty(), nullptr, "i");
    IRB.CreateStore(IRB.getInt64(0), iVar);
    IRB.CreateBr(loopBB);

    // 解密循环
    /*
    for(int i=0;i<strlen;i++)
    {
      result[i]=str[i]^key[i];
    }
    */
    IRB.SetInsertPoint(loopBB);
    Value *iLoad = IRB.CreateLoad(IRB.getInt64Ty(), iVar, "iLoad");
    Value *cond = IRB.CreateICmpULT(iLoad, IRB.getInt64(strSize), "cond");
    IRB.CreateCondBr(cond, loopbrBB, exitBB);

    IRB.SetInsertPoint(loopbrBB);
    Value *strPtr = IRB.CreateGEP(IRB.getInt8Ty(), strArg, iLoad, "strPtr");
    Value *strLoad = IRB.CreateLoad(IRB.getInt8Ty(), strPtr, "strLoad");
    Value *keyOffset =
        IRB.CreateURem(iLoad, IRB.getInt64(encKey.size()), "keyOffset");
    Value *keyIndex[] = {IRB.getInt32(0), keyOffset};
    Value *keyPtr =
        IRB.CreateInBoundsGEP(keyArrayType, keyVar, keyIndex, "keyPtr");
    Value *keyLoad = IRB.CreateLoad(IRB.getInt8Ty(), keyPtr, "keyLoad");
    Value *xorValue = IRB.CreateXor(strLoad, keyLoad, "xorValue");
    Value *outPtr = IRB.CreateGEP(IRB.getInt8Ty(), result, iLoad, "outPtr");
    IRB.CreateStore(xorValue, outPtr);
    Value *iNext = IRB.CreateAdd(iLoad, IRB.getInt64(1), "iNext");
    IRB.CreateStore(iNext, iVar);
    IRB.CreateBr(loopBB);

    IRB.SetInsertPoint(exitBB);
    IRB.CreateRet(result);
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

  bool insertDecryptoCall() {
    if (!decFunc)
      return false;

    if (isDouble) {
      for (User *user : callUser) {
        auto *instr = dyn_cast<Instruction>(user);
        if (!instr)
          return false;

        IRBuilder<> IRB(instr);
        // char ** tmp = dec(*ptr)
        Value *tmpVar = IRB.CreateAlloca(IRB.getPtrTy(), nullptr);
        Value *arg = IRB.CreateLoad(IRB.getPtrTy(), strVar);
        IRB.CreateStore(IRB.CreateCall(decFunc, {arg}), tmpVar);
        instr->replaceUsesOfWith(strVar, tmpVar);
      }
      return true;
    }

    for (User *user : callUser) {
      auto *instr = dyn_cast<Instruction>(user);
      if (!instr)
        return false;

      IRBuilder<> IRB(instr);
      Value *strPtr = IRB.CreateBitCast(strVar, IRB.getPtrTy());
      instr->replaceUsesOfWith(strVar, IRB.CreateCall(decFunc, {strPtr}));
    }
    return true;
  }
};

PreservedAnalyses EncryptoStrPass::run(Module &M, ModuleAnalysisManager &MAM) {
  errs() << "[vllvm] EncryptoStrPass:" << M.getName() << "\n";
  bool isChanged = false;
  std::vector<EncryptoStr *> encryptoStrPool = makeEncryptoStrPool(M);
  for (EncryptoStr *encryptoStr : encryptoStrPool) {
    if (!encryptoStr->insertDecryptoCall()) {
      isChanged = false;
      break;
    }
    isChanged = true;
  }
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

    // 加密字符串
    encryptoStr->encryptoStr();
    encStringPool.insert(encStringPool.end(), pendingDoubleStrings.begin(),
                         pendingDoubleStrings.end());
    encStringPool.push_back(encryptoStr);
  }
  return encStringPool;
}
