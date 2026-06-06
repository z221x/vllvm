#include "EncryptoStrPass.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Instructions.h"
#include "llvm/Support/RandomNumberGenerator.h"
class EncryptoStrPass::EncryptoStr {
public:
  uint64_t strID; // 用于标识以及密钥生成
  GlobalVariable *strVar;
  uint8_t *encKey;
  std::vector<User *> callUser;
  Function *decFunc;
  Module &M;
  bool isDouble;
  EncryptoStr(uint64_t id, GlobalVariable *strvar, bool isdouble, Module &m)
      : strID(id), strVar(strvar), isDouble(isdouble), decFunc(nullptr), M(m) {
    // 生成key
    getRandomKey();
    makeDecryptoFunc();
  };
  void encryptoStr() {
    ConstantDataArray *cda =
        dyn_cast<ConstantDataArray>(strVar->getInitializer());
    StringRef strValue = cda->getAsString();
    strValue = encrypto(strValue);
    // 修改构造器
    Constant *newInit =
        ConstantDataArray::getString(M.getContext(), strValue, false);
    strVar->setInitializer(newInit);
    // 设置可写
    strVar->setConstant(false);
  }
  StringRef encrypto(StringRef strRef) {
    unsigned char *str = (unsigned char *)strRef.data();
    for (int i = 0; i < strRef.size(); i++) {
      str[i] ^= encKey[i % 16];
    }
    return StringRef((const char *)str);
  }
  void makeDecryptoFunc() {
    if (!isDouble) {
      LLVMContext &Ctx = M.getContext();
      IRBuilder<> IRB(Ctx);
      std::vector<Type *> paramTypes = {IRB.getPtrTy()};
      FunctionType *decryptoFuncType =
          FunctionType::get(IRB.getPtrTy(), paramTypes, false);
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
      // 创建result
      llvm::Function *mallocFunc = M.getFunction("malloc");
      Value *result = IRB.CreateCall(
          mallocFunc,
          {IRB.getInt64(strVar->getValueType()->getArrayNumElements())});
      // 创建变量key
      llvm::ArrayType *I8ArrayType = llvm::ArrayType::get(IRB.getInt8Ty(), 16);
      Value *keyVar = IRB.CreateAlloca(I8ArrayType, nullptr, "key");
      for (int i = 0; i < 16; ++i) {
        llvm::Value *index[] = {
            llvm::ConstantInt::get(llvm::Type::getInt32Ty(Ctx),
                                   0), // 数组索引
            llvm::ConstantInt::get(llvm::Type::getInt32Ty(Ctx),
                                   i) // 元素索引
        };
        llvm::Value *keyElemPtr = IRB.CreateGEP(I8ArrayType, keyVar, index);
        llvm::Value *keyValue =
            llvm::ConstantInt::get(IRB.getInt8Ty(), encKey[i]);
        IRB.CreateStore(keyValue, keyElemPtr); // 存储元素值
      }
      // 创建变量i=0
      Value *iVar = IRB.CreateAlloca(IRB.getInt64Ty(), nullptr, "i");
      IRB.CreateStore(IRB.getInt64(0), iVar);
      // 解密循环
      /*
      for(int i=0;i<strlen;i++)
      {
        result[i]=str[i]^key[i];
      }
      */
      IRB.CreateBr(loopBB);
      IRB.SetInsertPoint(loopBB);
      Value *iLoad = IRB.CreateLoad(IRB.getInt64Ty(), iVar, "iLoad");
      Value *cond = IRB.CreateICmpSLT(
          iLoad, IRB.getInt64(strVar->getValueType()->getArrayNumElements()),
          "cond");
      IRB.CreateCondBr(cond, loopbrBB, exitBB);
      IRB.SetInsertPoint(loopbrBB);
      Value *strPtr = IRB.CreateGEP(IRB.getInt8Ty(), strArg, iLoad, "strPtr");
      Value *strLoad = IRB.CreateLoad(IRB.getInt8Ty(), strPtr, "strLoad");
      Value *kiLoad = IRB.CreateURem(iLoad, IRB.getInt64(0x10), "");
      Value *keyPtr = IRB.CreateGEP(IRB.getInt8Ty(), keyVar, kiLoad, "keyPtr");
      Value *keyLoad = IRB.CreateLoad(IRB.getInt8Ty(), keyPtr, "keyLoad");
      Value *xorValue = IRB.CreateXor(strLoad, keyLoad, "xorValue");
      Value *outPtr = IRB.CreateGEP(IRB.getInt8Ty(), result, iLoad, "outPtr");
      IRB.CreateStore(xorValue, outPtr);
      Value *iNext = IRB.CreateAdd(iLoad, IRB.getInt64(1), "iNext");
      IRB.CreateStore(iNext, iVar);
      IRB.CreateBr(loopBB);
      IRB.SetInsertPoint(exitBB);
      IRB.CreateRet(result);
    } else {
      decFunc = M.getFunction("_decrypto" + std::to_string(strID));
    }
  }
  void getRandomKey() {
    encKey = new uint8_t(16);
    // 根据strID生成唯一的RandomNumberGenerator
    std::unique_ptr<RandomNumberGenerator> RNG =
        M.createRNG(std::to_string(strID));
    uint64_t part1 = (*RNG)(); // 前8字节
    uint64_t part2 = (*RNG)(); // 后8字节
    memcpy(encKey, &part1, 8);
    memcpy(encKey + 8, &part2, 8);
    return;
  }
  bool insertDecryptoCall() {
    if (isDouble) {
      for (User *user : callUser) {
        Instruction *instr = dyn_cast<Instruction>(user);
        // errs() << *instr << "\n";
        IRBuilder<> IRB(instr);
        // char ** tmp = dec(*ptr)
        Value *tmpVar = IRB.CreateAlloca(IRB.getPtrTy(), nullptr);
        Value *arg = IRB.CreateLoad(IRB.getPtrTy(), strVar);
        IRB.CreateStore(IRB.CreateCall(decFunc, {arg}), tmpVar);
        // Value *retPtr = IRB.CreateBitCast(ret, IRB.getPtrTy());
        instr->replaceUsesOfWith(strVar, tmpVar);
        // errs() << *instr << "\n";
        //  instr->replaceUsesOfWith(strVar, ret);
      }
    } else {
      for (User *user : callUser) {
        if (Instruction *instr = dyn_cast<Instruction>(user)) {
          // s errs() << *instr << "\n";
          IRBuilder<> IRB(instr);
          Value *strPtr = IRB.CreateBitCast(strVar, IRB.getPtrTy());
          instr->replaceUsesOfWith(strVar, IRB.CreateCall(decFunc, {strPtr}));
        }
      }
    }
    return true;
  }
};
PreservedAnalyses EncryptoStrPass::run(Module &M, ModuleAnalysisManager &MAM) {
  errs() << "[vllvm] EncryptoStrPass:" << M.getName() << "\n";
  bool isChanged = false;
  LLVMContext &Ctx = M.getContext();
  IRBuilder<> IRB(Ctx);
  FunctionType *mallocType =
      FunctionType::get(IRB.getPtrTy(), {IRB.getInt64Ty()}, false);
  Function *mallocFunc =
      Function::Create(mallocType,
                       GlobalValue::ExternalLinkage, // 外部链接：实现来自标准库
                       "malloc", // 函数名必须为"malloc"
                       &M);
  std::vector<EncryptoStr *> encryptoStrPool = makeEncryptoStrPool(M);
  for (EncryptoStr *encryptoStr : encryptoStrPool) {
    // errs() << encryptoStr->strVar->getName() << encryptoStr->isDouble <<
    // "\n";
    if (!encryptoStr->insertDecryptoCall()) {
      isChanged = false;
      break;
    }
    isChanged = true;
  }
  return isChanged ? PreservedAnalyses::none() : PreservedAnalyses::all();
}
std::vector<EncryptoStrPass::EncryptoStr *>
EncryptoStrPass::makeEncryptoStrPool(Module &M) {
  std::vector<EncryptoStr *> encStringPool;
  uint64_t ID = 0;
  for (GlobalVariable &globalVar : M.globals()) {
    ID++;
    // 全局变量有构造器
    if (globalVar.hasInitializer() && globalVar.getValueType()->isArrayTy()) {
      ConstantDataArray *cda =
          dyn_cast<ConstantDataArray>(globalVar.getInitializer());
      // 字符串
      if (cda->isString()) {
        EncryptoStr *encryptoStr = new EncryptoStr(ID, &globalVar, false, M);
        // 第一层调用
        for (User *userFirst : globalVar.users()) {
          if (Instruction *instr = dyn_cast<Instruction>(userFirst)) {
            encryptoStr->callUser.push_back(userFirst);
          }
          // 如果使用者是全局变量，则寻找使用者的调用
          else if (GlobalVariable *global =
                       dyn_cast<GlobalVariable>(userFirst)) {
            EncryptoStr *encryptoStrDouble =
                new EncryptoStr(ID, global, true, M);
            for (User *userSeconde : global->users()) {
              if (Instruction *instr = dyn_cast<Instruction>(userSeconde)) {
                encryptoStrDouble->callUser.push_back(userSeconde);
              } else {
                // 如果有第三层引用就不在加密此字符串
                goto end;
              }
            }
            encStringPool.push_back(encryptoStrDouble);
          }
        }
        // 加密字符串
        encryptoStr->encryptoStr();
        encStringPool.push_back(encryptoStr);
      }
    } else {
    end:
      continue;
    }
  }
  return encStringPool;
}