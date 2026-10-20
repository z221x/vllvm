# VLLVM 项目进度

> 更新时间：2026-09-28。本文件记录各 Pass 的实现状态、构建/测试情况与后续计划，
> 与 `README.md`（使用说明）、`docs/VMP_DESIGN.md`（VMP 设计）互为补充。

## 一、当前架构

```text
src/
├── Pass/          Pass 实现（vmfla/ibr/icall/enstr/bb2func/merge/vmp）
├── attribute/     注解 → 函数属性/VLLVMOptions 的解析（VLLVMAttribute）
├── common/        公共工具（Utils、CryptoUtils 随机源）
├── config/        全局混淆等级配置（VLLVMConfig）
├── include/       Pass 与公共头文件（同步到 LLVM 树的 VLLVM include）
├── VMP/           实验性 LLVM VMP target + VmpRuntimeEmbed（嵌入解释器 bitcode）
└── c_func/        用户侧运行时：vminterpreter（VM 解释器）、funccaller（icall 蹦床）
```

构建流程：`build-{macos,linux}.sh` / `build-windows.ps1` 把 `src/` 分层镜像进
`llvm-project-21.1.0` 的 `llvm/lib/Transforms/VLLVM` 与 `llvm/lib/Target/VMP`，
应用 `patches/` 两个补丁后以 Ninja 增量构建 clang/clangd/lld/llc。

## 二、Pass 状态总览

| Pass | 注解 | 功能 | 状态 | 等级语义 |
|------|------|------|------|----------|
| EncryptoStr | `vllvm:enstr` | 字符串/常量加密（匿名内存池 + 下标访问） | ✅ 可用 | 0-2：1=字符串，2=再加密常量 |
| IndirectBranch | `vllvm:ibr` | 间接跳转 | ✅ 可用 | 0-3，≥1 等价（待差异化） |
| BogusControlFlow | `vllvm:bcf` | 独立虚假控制流（模块级共享导出 seed、函数内不初始化 + 对常量比较的恒定谓词 + 单后继分支伪造，真实/虚假块均可作虚假后继） | ✅ 可用 | 0-3：覆盖率 60%/85%/全量，虚假块数量递增 |
| IndirectCall | `vllvm:icall` | 间接调用（icallcc + X19 nest） | ✅ 可用（仅 AArch64） | 0-2：1=只间接调用，2=再混入参数加密与 fake 诱饵下标 |
| BB2Func | `vllvm:vmfla` 链路 | 基本块提取为 helper 函数 | ✅ 可用 | 随 vmfla 等级（0=整链关闭） |
| Merge | `vllvm:vmfla` 链路 | helper 融合为 keyed dispatcher | ✅ 可用 | 随 vmfla 等级（0=整链关闭） |
| VMFlattenFunc | `vllvm:vmfla` | VM 化平坦化（bb2func+merge+vmfla 三段链），内置平坦化与 lvars | ✅ 可用 | 0-2：1=bb2func+merge+平坦化，2=+局部变量结构化 |
| Vmp | `vllvm:vmp` | 函数虚拟化（VM 字节码 + 解释器运行时） | ✅ 可用（仅 AArch64） | 0-3，≥1 等价（待差异化） |

独立的 FlattenFuncPass（`vllvm:fla`）已删除：平坦化由 VMFlattenFuncPass
内置提供，`fla` 标注（含 IR 中遗留的 `vllvm.fla` 属性）归并进 vmfla 链路。
BogusControlFlowPass 保持独立（导出全局 seed 算法），`vllvm:bcf` 单独标注。

等级语义列指 `VLLVMConfig`（见下节）：每个 Pass 自己 `registerPassLevels`
声明区间并解释含义；enstr/icall/bcf/vmfla 已实现完整梯度，其余 Pass ≥1
等价于原行为，后续按 Pass 逐个补齐差异化。

## 三、全局混淆等级配置（新）

- 实现：`src/config/VLLVMConfig.{h,cpp}`，进程级单例。
- 命令行：`clang -mllvm -vllvm-config=<pass>=<level>[,...]`，
  例如 `-mllvm -vllvm-config=vmfla=3,icall=2`；`0` 表示全局关闭对应 Pass。
- Pass 侧约定：`run()` 入口调用 `registerPassLevels(name, default, max)`
  自声明区间，`getLevel(name)` 取生效等级，`isEnabled(name)` 做关闭闸门。
- 命令行解析发生在 Pass 注册之前：未注册的名字先记录，注册时收敛；
  越界等级钳制到 `[0, max]` 并打印 `[vllvm] VLLVMConfig:*: level clamped` 告警。
- vmfla：level 0=整条链路（bb2func+merge+vmfla）关闭；1=bb2func+merge+
  平坦化混淆；2=再开启局部变量结构化（lvars）。
- bcf（独立 pass）：前置处理 = 真实块大块切割（≥8 条指令切 4~6 条
  碎片（每段至少 4 条），避开 PHIs/入口前导 alloca/EH pad/musttail）+ 真实块指令克隆虚假块（保留
  值名/调试信息，跨块操作数替换） + 全块重排；运算统一 uint16（i16 回绕）；seed 为模块级单一
  **导出**全局变量（WeakAnyLinkage，符号名 `export_write`（不带 vllvm 前缀，避免自曝混淆器特征），所有 bcf 函数共享——首个 bcf
  函数创建、后续复用；多 TU 各自弱定义、链接器合并为全程序同一变量；外部库可按符号引用读取），
  **函数内不再初始化**（默认运行时值即初始化器 0），
  各分支点 volatile 读 seed 过单次**非零** add/sub/xor 运算（无乘法）后与随机常量比较，
  常量避开默认值 0 过同一运算的期望结果，创建极性恒定的虚假分支（`(seed op K) == C` 恒假
  / `!= C` 恒真按点随机，真假边排布对调）——恒定性以 seed 保持默认 0 为前提，外部库运行期
  改写可能翻转虚假边（约定只读），假边随机落入真实块或虚假块；虚假块链接成链并
  锚定在至少一条永不可达边后继上，保证进入后端发射与反编译视图；
  level 1/2/3 覆盖率 60%/85%/全量，虚假块数量递增。
- enstr：字符串重构为模块级**匿名内存池**，池逻辑（mmap、解密、下标
  访问）改为 C 实现 `src/c_func/enstrpool/enstrpool.c`，按解释器
  runtime 的方式编成位码（`EnstrPoolBitcode.inc`）由 pass 链入目标
  模块——pass 只生成描述表（u32 offset/size、8 字节密钥、isdec）与
  平台常量。2026-10-20 运行时协议改版：`__vllvm_enstr_get(i32 下标)`，
  密钥流为 key[j&7] 循环异或（pass 侧同步），去除 CAS 抢锁（单线程
  首次初始化语义，多线程并发首访不再保证恰好一次分配）；`__vllvm_
  enstr_count/pool_size` 随之收敛为 u32。调用点不再出现明文绝对地址；
  level 2 常量密钥经 volatile 读取，O2 也不会把解密折叠成明文常量。
  **目标平台优先 Android/ELF**（不再兼容 Windows）；mmap flags 按
  triple 生成（ELF 0x22 / Darwin 0x1002），最终由目标 libc 提供 mmap。
  level 1=字符串入池；level 2=再把作用域内标量整数常量（|v|≥256）
  换成 `trunc(volatile load 密文表 ^ K)`。
- icall：level 1=只做池化间接调用（icallcc 跳板）；level 2=目标入口/
  调用点再加可逆参数加密链（2026-10 移除 decoy 诱饵下标机制）。
- 测试：`test/config/test_config.sh` 覆盖 vmfla 0/1/2 档（lvars 逐级
  开启）与 bcf 独立等级、运行结果一致、钳制与非法片段告警；`test_enstr*.sh`
  覆盖池形态、下标访问、并发冷启动、OOM trap、i386/aarch64 交叉目标；
  `test_indirectcall.sh` 分别断言 level 1 无 crypt、level 2 有 crypt，
  且任何等级不再生成 decoy。

## 四、构建与测试状态（Windows x64 实测，2026-09-28）

### 通过
- 完整构建（clang/clangd/lld/llc + llvm-as/opt）
- `test_interpreter`（VM 解释器单测）、`test_target`（VMP 后端 llc）
- `test_lvars_fla`、`test_enstr`、`test_enstr_phi`、`test_indirectcall`、
  `test_icall_verifier`、`test_indirectbr`、`test_bb2func`、`test_bcf`、
  `test_merge`、`test_cross_targets`、`test_runtime_sdk`
- `test_config`（新增的全局配置测试）

### 失败（平台假设，非功能回归）
以下测试隐含 arm64 macOS 宿主或 POSIX 环境假设，在 x64 Windows 上按设计回退：
- `test_passes_complex`：icall 用例需 AArch64 triple 才会转换（x64 上回退）
- `test_vmp`：VMP 仅支持 64 位小端 AArch64；x64 上运行时输出仍一致，IR 断言失败
- `test_fallback`：aarch64 段未带 `--target`，x64 宿主上 remark 不匹配
- `test_enstr_cache`：驱动用了 POSIX `alarm()`
- `test_phi_lowering`：mingw 下 `__gxx_personality_v0` 链接失败

### Windows 跑测试的环境变量
`CLANG=<build>/llvm-windows/bin/clang.exe`、`LLVM_BIN=<build>/llvm-windows/bin`、
`LLVM_AS=<build>/llvm-windows/bin/llvm-as.exe`（测试脚本默认只探测 macos/linux 路径）。

## 五、已知问题
1. `test_vmfla_chain.sh` 第一个断言（`@vllvm.bb2f.` define 必须存在）与当前
   MergePass 行为矛盾：a545dc2 移除 func_table 后所有调用点直调 dispatcher，
   无引用成员会被内联删除（`MergePass.cpp` 收尾逻辑）。该断言在任何平台都会
   失败，需要确认后更新断言或恢复保留 wrapper 的行为。
2. 测试脚本的工具定位未包含 `build/llvm-windows`（靠环境变量绕过）。
3. `test_fallback.sh` 的 aarch64 段建议显式 `--target=aarch64-unknown-linux-gnu`，
   与 x86 段对齐后可跨平台。

## 六、后续计划
1. 各 Pass 补齐等级差异化语义（参照 bcf 模式：等级驱动强度/轮数/覆盖率）。
2. 修复第五节 1-3 项已知问题。
3. `VLLVMOptions`（bool 开关）与 `VLLVMConfig`（等级）的关系收敛：考虑把
   注解侧扩展为 `vllvm:<pass>@<level>` 的函数级覆盖，全局值作为默认。
4. VMP/解释器：非 AArch64 宿主的交叉工作流文档化；enstr_cache/phi_lowering
   的可移植夹具。
