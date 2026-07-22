# VLLVM VMP 设计文档

## 1. 文档信息

| 项目 | 内容 |
|---|---|
| 文档状态 | 设计草案 |
| 目标版本 | VMP MVP |
| LLVM 基线 | LLVM 21.1.0 |
| 当前 ISA 定义 | `src/include/VmpCommon.h` |
| 目标平台 | AArch64 macOS/Linux/Windows，其他目标安全回退 |

本文描述 VLLVM 中 Virtual Machine Protection（VMP）的完整设计，包括构建期
保护流程、VM 指令集、运行时、寄存器和栈模型、函数调用、执行表、解释器、
安全机制以及与 LLVM 的集成方式。

## 2. 背景与目标

VMP 将选定函数的原生实现替换为自定义字节码，并在运行时通过解释器执行。
攻击者不能再直接从宿主机器码中看到原始控制流和运算序列，而需要同时恢复：

- 字节码布局；
- Opcode 映射；
- 指令解密状态；
- VM 寄存器和栈语义；
- Handler 行为；
- 宿主调用映射。

### 2.1 设计目标

1. 按函数选择性保护，不影响未标记函数的正常原生编译。
2. 尽可能保持 LLVM IR 的整数、浮点、内存和控制流语义。
3. 使用寄存器型 VM，降低全内存型 VM 的解释开销。
4. 使用真实寄存器分配，只在寄存器压力过高时 spill 到 VM 栈。
5. 支持多线程并发，不使用进程级可变 VM 全局状态；递归不属于 V1 范围。
6. 字节码、常量池和调用信息可以按函数独立变换。
7. 不支持的函数安全回退为原生实现。
8. 将正确性、字节码生成和保护变换分层，便于测试和演进。

### 2.2 非目标

- VMP 不能阻止拥有完整运行时控制权的攻击者最终恢复语义。
- 首期不追求完整覆盖所有 LLVM IR，包括异常、向量和复杂原子操作。
- 首期加密不作为长期密钥存储方案。
- VMP 不提供独立的系统级内存安全沙箱。
- 不兼容 eBPF、JVM、WebAssembly 等现有字节码格式。

### 2.3 当前虚拟 CPU 固定基线

本设计不重新发明另一套 VM，以下结构直接来自当前 `VmpCommon.h`，后续编译器、
解释器和保护层都必须遵守：

| 项目 | 当前决定 |
|---|---|
| 指令宽度 | 固定 64 位 |
| 通用寄存器 | `R0-R13`，无类型 64 位位模式 |
| 专用寄存器 | `SA` 和 `ZR` |
| 浮点寄存器 | 不单独设置，浮点值同样保存在 `R0-R13` |
| 标志寄存器 | 不设置；比较结果写入普通寄存器 |
| PC | 解释器隐藏状态，不占 4 位寄存器编号 |
| 返回地址 | V1 无 VM 调用栈；`RET` 结束当前顶层解释执行 |
| 栈方向 | VM 栈区域向高地址增长，`SA` 指向当前帧起点 |
| 分支单位 | 有符号 32 位指令相对偏移，不是字节偏移 |
| 常量 | 32 位立即数或函数级 64 位常量池 |
| 指令类型 | 由 Opcode、Aux 和 InstFormat 共同决定 |

因此本设计不会恢复早期方案中的独立浮点寄存器、FLAGS、SP/BP，也不会把所有
LLVM Value 固定映射到内存槽。`SA` 是唯一暴露给指令的 VM 栈帧地址寄存器。

## 3. 威胁模型

### 3.1 主要对手能力

假设攻击者能够：

- 静态分析最终二进制；
- 定位解释器和 Handler；
- dump 进程内存；
- 动态跟踪解释器执行；
- 修改字节码或跳过格式与边界检查；
- 对多个受保护函数进行差分分析。

### 3.2 保护目标

VMP 主要提高以下工作的成本：

- 从原生指令快速恢复函数控制流；
- 使用通用反编译器直接生成伪代码；
- 对多个函数复用固定 Opcode 签名；
- 静态提取关键常量；
- 简单 patch 某个条件分支或返回值。

### 3.3 安全边界

解释器、字节码和密钥最终必须在本地运行，因此不能保证不可提取。设计目标是
增加分析成本、削弱自动化工具效果，并通过函数级差异化避免一次恢复全部函数。

## 4. 系统总体架构

VMP 分为构建期保护器和运行时解释器两部分。

```text
                        构建期

源代码 -> Clang -> LLVM Module
                       |
                       +-- 普通函数 -> 宿主后端 -> 原生机器码
                       |
                       `-- vmp 标记函数
                              |
                              +-- 资格检查
                              +-- IR 合法化
                              +-- VMP 指令选择
                              +-- 寄存器分配
                              +-- 字节码编码与验证
                              +-- （M5）Opcode 映射/加密
                              +-- 嵌入代码/常量全局表
                              `-- 原函数替换为包装函数

                        运行时

调用包装函数
   -> 创建 VMContext
   -> 参数封送到 R0-R5/参数区
   -> 加载 Code/Value/Function 表
   -> 取指、解密、解码、分派、执行
   -> RET 将 R0 封送为宿主返回值
   -> 销毁 VMContext
```

### 4.1 构建期组件

| 组件 | 职责 |
|---|---|
| `VmpPass` | 识别标记函数并编排完整虚拟化流程 |
| `VmpEligibility` | 检查函数是否属于当前支持范围 |
| `VmpIRPreparation` | 降低复杂 IR，规范类型和控制流 |
| `VMP Target` | 将 LLVM IR 编译为明文 VMP 指令 |
| `VmpObjectExtractor` | 提取代码、常量和符号引用 |
| `VmpTableBuilder` | 构造独立代码表、常量表和 HOSTCALL 目标地址表 |
| `VmpTransformer` | 执行 Opcode 映射、常量编码和指令加密 |
| `VmpModuleEmbedder` | 嵌入全局表并生成原生包装函数 |

### 4.2 运行时组件

| 组件 | 职责 |
|---|---|
| `VMContext` | 保存单次执行的寄存器、PC、栈和错误状态 |
| `VmpDecoder` | 解密并解码当前指令 |
| `VmpDispatcher` | 将逻辑 Opcode 分派到 Handler |
| `VmpHandlers` | 实现算术、内存、控制流和调用语义 |
| `VmpCallBridge` | 完成 VM ABI 与宿主 ABI 的转换 |
| `VmpVerifier` | 检查表参数、边界和 Opcode/Format 合法性 |

## 5. VM 执行模型

### 5.1 基本属性

- 寄存器型 VM；
- 每条指令固定 64 位；
- 指令地址按 8 字节对齐；
- 默认采用小端编码；
- 寄存器保存无类型 64 位位模式；
- Opcode 决定整数、浮点或地址解释方式；
- 比较结果写入普通寄存器，不依赖全局 FLAGS；
- PC 和解密状态属于隐藏解释器状态。

当前 VM 的“无类型”只表示运行时寄存器不携带类型标签。LLVM lowering 期间仍然
必须知道每个值的位宽和解释方式，并据此选择 Opcode、Aux 和显式转换指令。

### 5.2 运行时上下文

概念结构如下，最终实现不要求完全使用该内存布局：

```cpp
struct VMContext {
  std::uint64_t regs[16];

  const std::uint64_t *code;
  std::uint32_t instructionCount;
  const std::uint64_t *valueTable;
  std::uint32_t valueCount;
  const void *const *functionTable;
  std::uint32_t pc;

  std::byte *stackBase;
  std::uint32_t stackSize;
  VmpTrap trap;
};
```

每次顶层调用创建独立 `VMContext`。函数执行表是只读共享数据，因此同一个受保护
函数可以被多个线程并发执行。V1 不提供 VMCALL；受支持的直接调用通过统一
HOSTCALL bridge 进入宿主 AArch64 C ABI。

禁止将寄存器数组、PC、当前函数或动态解密状态保存在全局变量中。

## 6. 寄存器设计

### 6.1 寄存器集合

| 编码 | 名称 | 含义 |
|---:|---|---|
| 0-13 | `R0-R13` | 无类型 64 位通用寄存器 |
| 14 | `SA` | 当前 VM 栈帧基址 |
| 15 | `ZR` | 零寄存器；读取为 0，写入丢弃 |

不单独设置 FLAGS。整数和浮点比较将布尔值写入目的寄存器：

```text
R3 = ICMP.SLT R1, R2
BRCOND R3, target
```

这使多个 LLVM `i1` 值可以同时存活，也便于 LLVM 执行寄存器分配。

### 6.2 寄存器 ABI

| 寄存器 | ABI 用途 |
|---|---|
| `R0` | 参数 0、返回值 |
| `R1-R5` | 参数 1-5 |
| `R6-R11` | 调用保存寄存器 |
| `R12-R13` | 易失临时寄存器 |
| `SA` | 保留，当前栈帧基址 |
| `ZR` | 保留，常量零 |

调用约定：

```text
Caller-saved: R0-R5, R12-R13
Callee-saved: R6-R11
Reserved:     SA, ZR
Return:       R0
```

MVP 可以在每次调用前保守地 spill 所有跨调用活跃值；稳定后再利用
callee-saved 集合降低开销。

## 7. 指令编码

### 7.1 64 位布局

当前布局以 `src/include/VmpCommon.h` 为唯一编码依据：

```text
 63          52 51      48 47  44 43  40 39  36 35             4 3   1 0
+--------------+----------+------+------+------+----------------+-----+-+
| opcode (12)  | aux (4)  | dst  | src1 | src2 | payload (32)   | fmt |E|
+--------------+----------+------+------+------+----------------+-----+-+
```

| 字段 | 位宽 | 说明 |
|---|---:|---|
| Opcode | 12 | 逻辑 Opcode 或映射后的 Token |
| Aux | 4 | 谓词、值宽度或 Handler 变体 |
| Dst | 4 | 目的寄存器或 STORE 基址 |
| Src1 | 4 | 第一源寄存器或 STORE 数据 |
| Src2 | 4 | 第二源寄存器 |
| Payload | 32 | 立即数、偏移、索引或调用 ID |
| Format | 3 | 操作数解释格式 |
| E | 1 | 指令编码模式标志 |

`Aux` 由 Opcode 解释：

- `ICMP`：`IntPredicate`；
- `FCMP32/FCMP64`：`FloatPredicate`；
- 转换指令：`ValueWidth` 或转换变体；
- 其他指令：保留或作为 Handler 变体。

### 7.2 指令格式

| Format | 语义 |
|---|---|
| `RRR` | `Dst = op(Src1, Src2)` |
| `RRI` | `Dst = op(Src1, sext(Payload))` |
| `MEM_SRC` | `Dst = load(Reg[Src1] + sext(Payload))` |
| `MEM_DST` | `store(Reg[Dst] + sext(Payload), Reg[Src1])` |
| `REL32` | PC 相对跳转 |
| `CONST_POOL` | 格式编号保持冻结；Payload 是 ValueTable 索引 |
| `CALL` | HOSTCALL 的高 8 位 argc 和低 24 位 FunctionTable index |
| `NONE` | 无显式操作数 |

`REL32` 以指令数为单位，以当前指令的下一条指令为基准：

```text
payload = targetIndex - (currentIndex + 1)
```

解释器计算：

```text
nextPC = currentPC + 1 + sign_extend(payload)
```

### 7.3 Opcode 分类

| 类别 | Opcode |
|---|---|
| 数据移动 | `NOP/MOV/LDC` |
| 整数算术 | `ADD/SUB/MUL/UDIV/SDIV/UREM/SREM` |
| 位运算 | `AND/OR/XOR/NOT/SHL/LSHR/ASHR` |
| 整数比较 | `ICMP` |
| 类型转换 | `TRUNC/ZEXT/SEXT/BITCAST/SITOFP/UITOFP/FPTOSI/FPTOUI/FPEXT/FPTRUNC` |
| 浮点 | `FADD/FSUB/FMUL/FDIV/FNEG/FCMP` 的 32/64 位版本 |
| 内存 | `LOAD8/16/32/64`、`STORE8/16/32/64` |
| 控制流 | `BR/BRCOND/HOSTCALL/RET`；`VMCALL` 编号永久保留但非法 |
| 异常 | `TRAP` |

### 7.4 宽度语义

寄存器本身是 64 位，但小整数操作必须保持 LLVM 位宽语义：

- `i8/i16/i32` 结果在需要时显式 `TRUNC`；
- 有符号使用前显式 `SEXT`；
- 无符号使用前显式 `ZEXT`；
- 移位、除法和比较按照操作值宽度执行；
- 整数普通算术采用对应宽度的模运算。

LLVM 的 `nsw/nuw/exact` 是 IR 优化语义。进入最终 VMP CodeGen 后，不为其增加
新的运行时标志；对于本来产生 poison 或未定义行为的输入，VM 可以选择 trap，
但不能影响定义良好的程序。

### 7.5 基于当前结构的字段合法性

最终编码前必须把每条机器指令规范化成下表形式。表中“忽略”的字段由编码器写
成 `ZR` 或零，解释器不能依赖其中残留的数据。

| Opcode 类别 | Format | Dst | Src1 | Src2 | Payload | Aux |
|---|---|---|---|---|---|---|
| `NOP` | `NONE` | 忽略 | 忽略 | 忽略 | 0 | 0 |
| `MOV` | `RRR` | 目的 | 源 | 忽略 | 0 | 0 |
| `LDC` | `CONST_POOL` | 目的 | 忽略 | 忽略 | ValueTable 索引 | 0 |
| 二元整数运算 | `RRR` | 目的 | 左值 | 右值 | 0 | `ValueWidth` |
| 带立即数整数运算 | `RRI` | 目的 | 左值 | 忽略 | 有符号立即数 | `ValueWidth` |
| `NOT` | `RRR` | 目的 | 源 | 忽略 | 0 | `ValueWidth` |
| `ICMP` | `RRR` | 布尔结果 | 左值 | 右值 | 0 | `IntPredicate` |
| 整数/浮点转换 | `RRR` | 目的 | 源 | 忽略 | 0 | 转换模式 |
| 二元浮点运算 | `RRR` | 目的 | 左值 | 右值 | 0 | 0 |
| `FNEG32/FNEG64` | `RRR` | 目的 | 源 | 忽略 | 0 | 0 |
| `FCMP32/FCMP64` | `RRR` | 布尔结果 | 左值 | 右值 | 0 | `FloatPredicate` |
| `LOAD*` | `MEM_SRC` | 目的 | 基址 | 忽略 | 有符号字节偏移 | 0 |
| `STORE*` | `MEM_DST` | 基址 | 数据 | 忽略 | 有符号字节偏移 | 0 |
| `BR` | `REL32` | 忽略 | 忽略 | 忽略 | 有符号指令偏移 | 0 |
| `BRCOND` | `REL32` | 忽略 | 条件值 | 忽略 | 有符号指令偏移 | 0 |
| `HOSTCALL` | `CALL` | 忽略 | 忽略 | 忽略 | `argc:index(8:24)` | 0 |
| `VMCALL` | 不适用 | 忽略 | 忽略 | 忽略 | 忽略 | 永久非法 |
| `RET` | `NONE` | 忽略 | 忽略 | 忽略 | 0 | 0 |
| `TRAP` | `NONE` | 忽略 | 忽略 | 忽略 | 可选 trap 原因 | 0 |

允许多个 Format 的 Opcode 只包括明确支持 `RRR/RRI` 的整数二元运算。解释器遇到
其他 Opcode/Format 组合必须产生 `INVALID_FORMAT`。

### 7.6 当前 Aux 字段的转换规则

当前 4 位 Aux 足以编码比较谓词，但不能同时直接保存任意“源宽度 + 目标宽度”。
为了不修改现有 64 位布局，MVP 采用以下规范化规则：

- `TRUNC`：Aux 保存目标整数 `ValueWidth`；
- `ZEXT/SEXT`：Aux 保存源整数 `ValueWidth`，结果规范化为 64 位位模式；
- `BITCAST`：只允许等位宽转换，运行时等价于 `MOV`；
- `SITOFP/UITOFP`：较小整数先扩展到 i32/i64，Aux 编码
  `i32/i64 -> f32/f64` 四种组合；
- `FPTOSI/FPTOUI`：Aux 编码 `f32/f64 -> i32/i64` 四种组合，小整数结果再
  `TRUNC`；
- `FPEXT`：固定为 `f32 -> f64`；
- `FPTRUNC`：固定为 `f64 -> f32`。

应在 `VmpCommon.h` 增加独立的 `ConversionMode` 枚举作为 Aux 的规范值，不能把
所有转换都错误地直接解释为单个 `ValueWidth`。

### 7.7 具体运算语义

- `UDIV/UREM` 按无符号值解释操作数；
- `SDIV/SREM` 按指定整数宽度的二进制补码解释操作数；
- 除数为零产生 `DIVIDE_BY_ZERO`；
- 有符号最小值除以 `-1` 产生 `SIGNED_DIVIDE_OVERFLOW`；
- `SHL/LSHR/ASHR` 的移位量必须小于值宽度，否则产生 trap；
- `LSHR` 补零，`ASHR` 使用符号位填充；
- `ICMP` 始终只向 Dst 写入规范化的 `0` 或 `1`；
- `BRCOND` 只判断 `Reg[Src1] != 0`，不读取不存在的 FLAGS；
- `RET` 从 `R0` 取得返回值；
- 对 `ZR` 的任何写入都被丢弃；
- 除受控的帧进入/退出逻辑外，普通指令禁止把 `SA` 作为 Dst。

## 8. 常量池

32 位 Payload 无法容纳完整的 `i64`、指针或 `double`，因此使用函数级常量池：

```text
LDC R3, constant_pool[7]
```

ValueTable 条目统一为 64 位原始位模式。M3 只生成整数和空指针常量；类型解释由
选择该 LDC 的编译期上下文决定，运行时常量表不附带类型标签。

可以放入有符号 32 位 Payload 的整数优先使用 `RRI`；其余常量使用 `LDC`。
常量去重在函数内部进行，默认不跨函数共享，避免形成稳定的全局关联特征。

## 9. 内存与栈模型

### 9.1 VM 栈

每个 `VMContext` 使用包装函数在宿主栈分配的独立连续区域。进入解释器时：

```text
stackSize = frameSize
SA = address(stack)
```

这里冻结当前语义：`SA` 保存包装函数所分配 VM 栈区域内的宿主地址，而不是抽象
FrameIndex 或整数槽编号。编译期间可以使用 FrameIndex，必须在生成最终字节码前
消除成 `SA + signedPayload`。

函数执行 `RET` 时把 `R0` 交给顶层包装函数并结束当前 VMContext。

一个栈帧包括：

```text
+--------------------------+ <- SA
| 固定 alloca              |
| spill slots              |
| PHI/并行复制临时空间     |
| 传栈参数区               |
+--------------------------+ <- SA + frameSize
```

V1 没有隐藏调用栈，也不在 VM 栈帧中保存返回 PC 或调用者状态。

### 9.2 LOAD/STORE

```text
LOAD64  R1, [R2 + offset]
STORE64 [R2 + offset], R1
```

执行地址为：

```text
effectiveAddress = Reg[base] + sign_extend(payload)
```

VM 栈地址必须检查是否处于当前 VM 栈区域。为了兼容原函数对宿主对象的访问，
寄存器也可能保存宿主指针；MVP 对宿主指针保持原始程序的访问语义，不将其宣称
为沙箱安全。

后续可以加入带标签地址或访问描述符，将 VM 栈、宿主对象、全局变量和只读常量
区分开，降低字节码被篡改后的任意内存访问能力。

### 9.3 对齐与端序

- 字节码采用固定小端编码；
- 内存操作遵守原 LLVM IR 的对齐要求；
- 不支持的平台非对齐访问由解释器使用 `memcpy` 语义实现；
- GEP 偏移必须使用原 Module 的 `DataLayout` 计算。

## 10. 控制流与 PHI

### 10.1 基本块

每个 LLVM 基本块映射为一段连续 VMP 指令。构建期记录块标签，所有指令生成后
统一修正 `BR/BRCOND` 的 REL32 Payload。

### 10.2 PHI

PHI 表示根据控制流前驱选择值。它不是运行时 Opcode，应在进入最终字节码前
转换为前驱边上的并行 COPY：

```llvm
merge:
  %x = phi i64 [ %a, %left ], [ %b, %right ]
```

概念上转换为：

```text
left  -> merge: x = a
right -> merge: x = b
```

使用 LLVM Machine CodeGen 时由标准 PHI 消除流程处理。critical edge 必要时
拆分；并行复制形成环时使用临时寄存器或临时 spill slot，不能顺序覆盖源值。

### 10.3 分支验证

解释器在执行分支时检查：

- 目标索引没有有符号溢出；
- 目标位于当前函数代码范围内；
- 目标指向完整指令；
- 不允许直接跳进其他函数的代码表。

## 11. 函数调用

### 11.1 顶层包装函数

受保护函数原函数体被替换为宿主包装函数：

```llvm
define i64 @foo(i64 %a, i64 %b) {
entry:
  %argv = alloca [2 x i64], align 8
  %stack = alloca [frameSize x i8], align 16
  %result = call i64 @__vllvm_vmp_execute(
      ptr @__vllvm_vmp_code.foo, i32 codeSize,
      ptr null,
      ptr @__vllvm_vmp_values.foo, i32 valueCount,
      ptr %argv, i32 2,
      ptr %stack, i32 frameSize)
  ret i64 %result
}
```

包装函数把参数零扩展或按位转换到 argvArray；void 返回会忽略解释器返回的 R0。

### 11.2 保留的 VMCALL 编号

`VMCALL=0x052` 只用于保持已经冻结的 Opcode 数值稳定。V1 不提供 VM 间调用、
VM 函数表或隐藏调用栈；验证器和解释器遇到该 Opcode 必须返回
`INVALID_OPCODE`。以后也不能把该编号重新解释为其他功能。

### 11.3 HOSTCALL

`HOSTCALL` 从当前受保护函数的 FunctionTable 读取真实宿主函数地址，再调用全局
唯一的 AArch64 C ABI bridge。
Payload 同时保存参数数量和表索引：

```text
31                 24 23                              0
+--------------------+--------------------------------+
| argc（8 位）       | FunctionTable index（24 位）   |
+--------------------+--------------------------------+
```

编译期扫描所有直接 `call` 并按原目标函数去重。所有目标共用：

```cpp
uint64_t bridge(uint64_t *registers, uint64_t *stackArgs,
                uint32_t argc, void *funcaddr);
```

前六个参数位于 `R0-R5`。第七、八个参数暂存在 `stackArgs[0..1]`，bridge 将其
装入 AArch64 `x6/x7`；第九个及后续参数由 Pass 按最终宿主 ABI 布局到
`stackArgs + 16`，bridge 复制到宿主 SP。寄存器小整数根据调用点的
`signext/zeroext` 属性提前扩展；macOS 栈参数则按其原生 1/2/4/8 字节紧凑规则
预排。专用 outgoing-argument 区位于 VM frame 尾部，按 16 字节对齐，
并与 alloca/spill 隔离。目标返回的 `x0` 直接作为 `uint64_t` R0；void 调用会忽略
该值。FunctionTable 保存真实目标地址，同一目标只保存一次。

统一 bridge 只支持默认 C 调用约定。LLVM 优化生成的 `fastcc` 目标仅在它是当前
Module 的内部定义、地址没有逃逸、全部用途都是非 musttail 的直接 fastcc 调用时，
才把目标及其所有调用点统一改回 C 调用约定；其他 fastcc 目标使候选函数原生回退。

## 12. 函数执行表与运行时 ABI

每个受保护函数使用彼此独立的只读全局表，不再构造包含 Header 和常量池的连续
镜像 Blob：

```text
VmpFunctionTables
├── Code[]          固定 64 位小端 VM 指令
├── ValueTable[]    独立的 64 位常量表，可为空
└── FunctionTable[] HOSTCALL 真实目标函数地址表，无直接调用时为空
```

`Code` 直接指向第一条指令，入口固定为 PC 0。`codeSize` 使用字节数且必须为
8 的非零整数倍，解释器通过 `codeSize / 8` 得到指令数量。常量不属于 vmcode，
`LDC payload` 是 `ValueTable` 的索引。

运行时 ABI 固定为：

```cpp
uint64_t __vllvm_vmp_execute(
    const uint64_t *code,
    uint32_t codeSize,
    const void *const *functionTable,
    const uint64_t *valueTable,
    uint32_t valueCount,
    uint64_t *argvArray,
    uint32_t argc,
    uint8_t *stack,
    uint32_t stackSize);
```

正常执行直接返回 R0。任何格式、边界、算术或显式 Trap 均由解释器内部调用
`llvm.trap` 终止，不通过返回值向包装函数报告。`functionTable` 不携带数量；没有
HOSTCALL 时包装函数传入空指针。VM 栈继续由包装函数根据已知 frameSize 在宿主栈
分配并通过最后两个参数传入。

### 12.1 LLVM Module 中的表示

```llvm
@__vllvm_vmp_code.foo = private constant [N x i64] [...]
@__vllvm_vmp_values.foo = private constant [M x i64] [...]
@__vllvm_vmp_functions.foo = private constant [K x ptr] [ptr @target0, ...]
```

没有常量时不创建 values 全局变量，并向解释器传入 `nullptr, 0`。代码表和存在的
常量表、函数表都加入 `llvm.compiler.used`，避免被无引用全局清理删除。没有直接
调用时不创建函数表并传入 `nullptr`。

### 12.2 表验证

解释器在取指前验证：code 非空、codeSize 合法、参数数量不超过 6、非空表与数量
一致、VM 栈不超过 1 MiB 且满足 16 字节对齐。每条指令执行前继续验证
Opcode/Format/Aux、分支目标和 ValueTable 索引。
FunctionTable 按已确认 ABI 不传数量，因此解释器只能检查表指针和取出的目标地址
是否为空；index 的 24 位范围由编译期保证。

## 13. 解释器设计

### 13.1 执行循环

```cpp
while (context.trap == VmpTrap::None) {
  checkPc(context);
  std::uint64_t encoded = fetch(context);
  Instruction inst = decryptAndDecode(context, encoded);
  verifyInstruction(context, inst);
  context.pc += 1;
  dispatch(context, inst);
}
```

PC 默认在执行 Handler 前加一，因此 REL32 Payload 为 0 时自然指向下一条指令。
跳转 Handler 在已经递增的 PC 上增加有符号 Payload。

### 13.2 分派方式

MVP 使用清晰、可验证的 `switch` 分派：

```cpp
switch (logicalOpcode) {
case Opcode::ADD:
  handleAdd(context, inst);
  break;
// ...
}
```

稳定后可选择：

- 函数指针 Handler 表；
- computed goto/threaded dispatch；
- 多套等价 Handler；
- 每函数不同的 Token 到 Handler 映射。

优化分派不能改变指令验证、trap 和边界检查语义。

### 13.3 Trap

建议定义：

```text
INVALID_OPCODE
INVALID_FORMAT
INVALID_REGISTER
PC_OUT_OF_RANGE
BRANCH_OUT_OF_RANGE
STACK_OVERFLOW
STACK_OUT_OF_RANGE
CONSTANT_OUT_OF_RANGE
DIVIDE_BY_ZERO
SIGNED_DIVIDE_OVERFLOW
INVALID_VM_CALL
INVALID_HOST_CALL
CALL_DEPTH_EXCEEDED
IMAGE_INTEGRITY_FAILURE
UNSUPPORTED_IMAGE_VERSION
EXPLICIT_TRAP
```

Trap 编号保持稳定，其中 VM 调用和镜像版本相关编号仅为 ABI 保留。公开解释器
接口不返回错误码；任何非 `None` Trap 都直接调用 `llvm.trap`，不得让格式错误的
字节码继续使用未初始化值执行。

### 13.4 指令合法性

代码表加载时和执行时共同验证：

- Opcode 是否存在；
- Opcode 与 Format 是否匹配；
- Aux 是否在该 Opcode 的允许范围；
- codeSize、参数/常量表数量与指针是否合法；
- 常量和分支索引是否合法；
- `SA/ZR` 是否被非法写入；

## 14. 编译期 IR 到字节码

### 14.1 处理流程

```text
标记函数
  -> 完整资格检查
  -> 克隆到临时 Module
  -> IR 规范化/合法化
  -> VMP 指令选择
  -> Machine SSA
  -> PHI 消除
  -> 活跃区间分析
  -> LLVM 寄存器分配
  -> spill/reload 和栈帧布局
  -> 分支/重定位修正
  -> 明文字节码验证
  -> 构造独立代码表和常量表
  -> 保护变换
  -> 嵌入原 Module
  -> 替换原函数
```

### 14.2 MVP 支持的 LLVM IR

- 标量 `i1/i8/i16/i32/i64`；
- 64 位指针；
- 整数算术、位运算、移位和比较；
- `trunc/zext/sext/bitcast`；
- 静态 `alloca`；
- 普通 `load/store`；
- `br`、条件 `br`、PHI、select/switch 降级、循环和 `ret`；
- 最多 255 个整数/指针参数的直接 `call`，以及整数、指针或 void 返回；
- 32 位立即数和函数级常量池。

后续增加：

- `f32/f64`；
- GEP；
- 全局符号；
- 聚合参数和返回值。

暂不支持：

- `invoke/landingpad/resume`；
- 原子和复杂内存序；
- vector/scalable vector；
- 可变参数；
- inline asm；
- 动态 `alloca`；
- 无法描述的间接调用。

### 14.3 安全回退

函数虚拟化必须是事务式操作。在字节码生成、全局表构造和包装函数全部成功之前，
不得删除或修改原函数体。

不支持时：

1. 产生包含函数名和失败原因的 Optimization Remark；
2. 保留原生函数；
3. 不留下不完整代码表或常量表；
4. 继续处理其他函数。

## 15. 真实寄存器分配

VMP 字节码生成不能从 LLVM Value 直接映射到 4 位物理寄存器。中间阶段必须使用
LLVM 虚拟寄存器：

```text
%3:gpr64 = ADDrr %1:gpr64, %2:gpr64
```

LLVM 通用寄存器分配器随后将其转换为：

```text
ADDrr R7, R2, R5
```

寄存器不足时自动插入：

```text
STORE64 [SA + spillOffset], R7
LOAD64  R4, [SA + spillOffset]
```

VMP Target 需要提供：

- `GPR64` 寄存器类；
- `SA/ZR` 保留规则；
- `copyPhysReg()`；
- `storeRegToStackSlot()`；
- `loadRegFromStackSlot()`；
- `eliminateFrameIndex()`；
- 调用保存寄存器规则。

## 16. LLVM BPF 后端的作用

BPF 后端只作为 VMP Target 的实现模板，因为它同样具有固定 64 位指令、4 位
寄存器编号和 32 位立即数字段。

从 `llvm/lib/Target/BPF` 复制并精简出 `llvm/lib/Target/VMP`：

```text
VMP/
├── VMP.td
├── VMPRegisterInfo.td
├── VMPInstrFormats.td
├── VMPInstrInfo.td
├── VMPCallingConv.td
├── VMPFrameLowering.{h,cpp}
├── VMPISelLowering.{h,cpp}
├── VMPISelDAGToDAG.cpp
├── VMPInstrInfo.{h,cpp}
├── VMPRegisterInfo.{h,cpp}
├── VMPSubtarget.{h,cpp}
├── VMPTargetMachine.{h,cpp}
├── VMPAsmPrinter.cpp
├── MCTargetDesc/
└── TargetInfo/
```

复用：

- LLVM Target 工程结构；
- TableGen 接入方式；
- SelectionDAG 流程；
- FrameLowering 接口；
- MC 编码和 Object 输出结构；
- LLVM 通用寄存器分配基础设施。

替换或删除：

- BPF Opcode 和编码；
- BPF 寄存器和 ABI；
- BPF verifier 约束；
- BTF、CO-RE、map 和 kernel helper；
- BPF 专用原子及内核语义。

BPF 不参与最终运行，解释器执行的是自定义 VMP 字节码。

## 17. 字节码保护

M3 只实现明文执行契约：逻辑 Opcode 使用恒等编码、所有指令 `E=0`，ValueTable
保存未经加密的 64 位原始值。下列保护变换属于 M5，且必须位于明文字节码生成、
分支修正和验证之后。

### 17.1 Opcode 映射

M5 可以让逻辑 Opcode 不直接作为最终 12 位值，并为每个函数生成独立映射：

```text
logical ADD -> token 0x7A3
logical SUB -> token 0x19C
```

解释器使用函数代码表关联的解码信息恢复逻辑 Opcode。不同函数的相同逻辑 Opcode
应尽量使用不同 Token。

### 17.2 指令加密

M3 固定 `E=0`。M5 若启用指令加密，`E` 位仍须保持可读；具体加密算法和随机
访问规则需在实现前另行冻结。

### 17.3 常量池保护

M3 不编码 ValueTable。M5 可以按函数独立编码常量，解码时结合常量索引和函数
密钥；字符串和指针重定位应在最终链接关系确定后处理，不能被普通整数加密破坏。


## 18. 并发与生命周期

- 函数代码表、常量表和函数表为只读、可共享对象；
- 每次顶层执行独立分配 `VMContext`；
- V1 不支持 VM 间调用或字节码递归；
- HOSTCALL 返回后恢复调用约定要求的状态；
- trap 或正常返回都必须释放顶层上下文资源；
- 不得把上一次执行的寄存器或解密状态泄漏到下一次调用。

为了降低频繁分配开销，可以使用线程局部 Context 池，但借出和归还时必须完全
重置可变状态。

## 19. 性能设计

主要开销来自：

- 每条指令的取指、解密、验证和分派；
- VM register 与宿主变量之间的封送；
- spill/reload；
- HOSTCALL trampoline；
- 格式和边界检查。

优化顺序：

1. 正确性和可验证性；
2. 真实寄存器分配，减少全栈访问；
3. 常用 RRI 指令，减少 LDC；
4. Handler 内联和高效分派；
5. 基本块级批量解密；
6. 合并冗余 MOV、扩展和 load/store；
7. 按保护等级选择检查和加密强度。

不建议第一版实现自修改原生代码或运行时生成可执行内存，以避免 W^X、代码签名
和平台安全策略问题。

## 20. 测试与验证

### 20.1 ISA 单元测试

- encode/decode 往返；
- 所有字段边界值；
- Opcode/Format 合法组合；
- signed Payload；
- REL32 正向、反向和零偏移；
- `ZR/SA` 规则；
- 每个 Handler 的正常和 trap 路径。

### 20.2 CodeGen 测试

- LLVM IR 到 VMP Opcode；
- PHI 和 critical edge；
- 循环和多出口控制流；
- 参数、返回值和调用保存规则；
- 超过 14 个活跃值时的 spill/reload；
- 栈帧偏移和 FrameIndex 消除；
- 分支 fixup 和常量池索引；
- `-verify-machineinstrs`。

### 20.3 差分测试

为测试函数同时保留原生版本和 VMP 版本，对相同输入比较：

- 返回值；
- 输出参数；
- 可观察内存副作用；
- 浮点位模式和 NaN 行为；
- trap 或失败状态。

### 20.4 鲁棒性测试

- 随机破坏 Opcode、Format、Payload、codeSize 和表指针/数量组合；
- 越界分支、栈和常量池访问；
- VM 栈耗尽；
- 多线程并发执行同一代码表；
- HOSTCALL 空 FunctionTable/目标地址和不足的 outgoing 栈区；
- 非 8 字节倍数 codeSize、空代码表和常量表越界。

## 21. 实施里程碑

### M0：冻结执行语义

- 确认当前指令编码；
- 建立 Opcode/Format 合法性表；
- 完成 encode/decode 测试。

### M1：最小整数 VM

- 实现全部 M3 整数、转换、访存和控制流 Handler；
- 手工构造代码表和 ValueTable 可以正确执行；
- VM 栈、PC、表参数和 Trap 检查可用；
- 生成并一致性检查可嵌入的 LLVM 21 bitcode。

### M2：LLVM 到明文字节码

- 建立 VMP Target；
- 支持基础整数 IR；
- LLVM 完成 PHI 和寄存器分配；
- 高寄存器压力产生正确 spill/reload。

### M3：端到端函数保护

- VmpPass 嵌套编译标记函数；
- 嵌入独立代码表和常量表；
- 替换原函数为包装函数；
- 直接 call 扫描、统一 HOSTCALL bridge、fastcc 规范化和溢出参数区；
- 原生/VMP 差分测试通过；
- 不支持函数安全回退。

### M4：完整标量语义

- GEP；
- `f32/f64`；

### M5：保护强化

- 函数级 Opcode 映射；
- 基本块可随机访问的指令加密；
- 常量池保护；
- Handler 变体和性能优化。

## 22. MVP 完成标准

同时满足以下条件才认为 MVP 完成：

1. 手工字节码和 LLVM 生成字节码共用同一套解释器语义；
2. `if/else`、循环、PHI、整数运算和普通内存访问正确；
3. LLVM 通用寄存器分配器实际参与分配并能产生 spill/reload；
4. 高压力函数可以正确 spill/reload；
5. 标记函数通过包装函数进入解释器执行；
6. 普通函数仍由宿主后端原生编译；
7. 不支持的函数不被破坏，并产生明确诊断；
8. VMContext 支持多线程并发；
9. native/VMP 差分测试通过；
10. 明文字节码稳定后再启用保护变换。

## 23. M3 之后的待确认设计项

M3 的栈上限/对齐、六参数限制、回退范围、`ConversionMode` 和 `E=0` 已冻结。
下列问题只影响明确排除在本轮之外的后续能力，不阻塞 M0-M3：

1. 宿主指针的验证或标签策略；
2. 浮点是否要求严格 IEEE/NaN 位模式一致；
3. M5 的 E 位语义、密钥派生和保护等级配置。

## 24. 参考资料

- [LLVM Writing an LLVM Backend](https://llvm.org/docs/WritingAnLLVMBackend.html)
- [LLVM Target-Independent Code Generator](https://llvm.org/docs/CodeGenerator.html)
- [LLVM 21.1.0 BPF Target](https://github.com/llvm/llvm-project/tree/llvmorg-21.1.0/llvm/lib/Target/BPF)
- [LLVM Greedy Register Allocator](https://github.com/llvm/llvm-project/blob/llvmorg-21.1.0/llvm/lib/CodeGen/RegAllocGreedy.cpp)
- [xVMP](https://github.com/GANGE666/xVMP)
