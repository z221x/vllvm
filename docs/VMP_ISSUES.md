# VMP 已知问题记录

来源：2026-10 开源应用性能测试与标准算法矩阵（详见
`docs/PASS_PERF_REPORT.md` 的「VMP 函数级开销」与「标准算法矩阵」两节；
数据见该报告）。按严重度排序，每条附复现方式
与建议修复方向。复现统一使用 `build/llvm-macos/bin/clang`（下称 `$CLANG`）。

## V-1 递归函数虚拟化后崩溃（SIGSEGV）——已修复

- 严重度：高（正确性）；状态：**已修复**（2026-10-12）
- 真实根因（非 hostcall 桥、非栈保护页）：**`DenseMap<uint64_t>` 的空桶
  哨兵 = `~0ULL`（-1）、墓碑 = `~0ULL-1`（-2）**。`VMPFunctionCompiler`
  的 `ConstantIndices` 用它做常量去重，`addConstant(-1)` 探测空桶时
  哨兵值恰好等于查找键 → try_emplace 误报“已存在”返回垃圾下标 0，
  **-1 永远进不了常量表**。`fib(n-1)` 的字节码 LDC 遂取 values[0]=2，
  实参变成 n+2 → 递归无限加深 → 8MB 栈耗尽（崩溃地址在栈 rlimit 底部
  之下，code=2）。此前的“解释器序言越保护页”理论不成立——栈一直在
  正常生长，是无限递归把它打穿。
- 修复：`ConstantIndices` 改用 `std::map`（无带外哨兵；常量数小，复杂度
  无影响），附注释说明哨兵撞键陷阱。
- 验证：手写同形 IR 复现常量表 `[2,-2]`（-1 丢失）→ 修复后 `[2,-1,-2]`；
  `fib(10)=55` 正确；VMP 全回归 6 项通过。
- 附带：同轮已把解释器改为自分配 VM 栈（execute 不再收 Stack 指针，
  递归时 VM 帧不占宿主栈）。递归仍受宿主栈 rlimit 约束（每层 ≈ 0x2D0
  字节），超深递归会正常 SIGSEGV——与原生语义一致，非缺陷。

## V-2 位运算 CRC 组合形态触发 VM 断言（SIGTRAP）

- 严重度：高（正确性）；状态：**未修复**
- 现象：`crc32_word`（位运算 CRC-32，无数组/无查表/无 GEP）准入后运行
  即 SIGTRAP；lldb 定位 **EXC_BREAKPOINT 落在 `__vllvm_vmp_execute` 内部**
  ——是 VM 执行字节码时的内部断言，不是访存越界。
- 隔离结论：变量移位（`word >> (8*i)`）、嵌套双层循环、32 位常量移位
  循环**逐个构造均正常**，仅完整组合触发；将取负 mask 换成 select 仍崩，
  排除取负运算与向量 IR。
- 最小复现：`test/perf/repro/vmp_crc32_trap.c`
  ```bash
  $CLANG -O2 -fno-vectorize -DALGOS_VMP test/perf/repro/vmp_crc32_trap.c -o t
  ./t    # native 正常打印 crc，vmp 版 SIGTRAP
  ```
- 建议：先给 `__vllvm_vmp_execute` 的断言点加诊断输出（opcode/pc/值栈
  深度），再用该复现定位；怀疑值栈深度或 32/64 位混合位运算路径。

## V-3 哈希类（SHA-1/SHA-256/MD5）完全不可虚拟化

- 严重度：中高（能力缺失，核心保护对象恰是这类算法）；状态：**未修复**
- 回退链（四条路全堵）：
  1. 任何优化级别下 instcombine 把旋转惯用法 `(x<<n)|(x>>(32-n))` 融合
     成 `llvm.fshl/fshr` intrinsic，M3 无对应指令 → 回退
     （VmpPass 挂在 OptimizerLast，必然看到融合结果）；
  2. volatile 屏障阻止融合 → M3 拒绝 volatile load/store；
  3. 消息字做参数（16 个 u32）→ 超过 **R0-R5 六参数上限**；
  4. 全局标量 + `-O0`（无 instcombine）→ “VMP Direct CodeGen 无法取得
     SSA 值：<global>”。
- 复现：`test/perf/workloads/vmp_algos.c` 的 `sha256_block`/`sha1_block`/
  `md5_block` 任一，`-Rpass-missed=vmp` 可见逐条回退原因。
- 建议（按解锁能力排序）：① M3 增加 fshl/fshr 指令（解锁全部旋转类
  算法）；② 放宽参数上限或支持参数经值栈传参；③ 重跑
  `python3 test/perf/vmp_fn_bench.py` 即可复测标准算法矩阵。

## V-4 准入排除面过大：真实项目热点全部回退

- 严重度：中（可用性）；状态：设计限制 + 待改进
- 现象（单函数标注探测，`nm` 查 `__vllvm_vmp_code.<fn>`）：
  - zlib：仅 `adler32`/`crc32` **薄包装**准入（循环在被调原生函数里，
    保护收益≈0）；`crc32_z`、`deflate`、`_tr_flush_block` 回退；
  - bzip2：`BZ2_hbAssignCodes`、`BZ2_hbMakeCodeLengths` 回退；
  - lua：`luaV_idiv`、`luaV_mod`、`luaV_tointegerns` 回退。
- 已知排除因素：GEP 不支持（一切数组/指针下标）、写内存循环排除
  （k_mem 合成内核也回退）、六参数上限、向量 IR（需 `-fno-vectorize`
  配合，见 V-6）。
- 建议：短期在文档明确“适用形态”；中期评估 GEP（常量下标）与参数上限。

## V-5 大范围回退时的“隐形”开销与静默性

- 严重度：中；状态：**未修复**
- 现象：
  - lua 全程序 vmp：**0 个热点被虚拟化**，运行时仍 1.48x——回退路径
    阻塞后续优化（`lvm.o` 代码段 17665→22982 字节，+30%）；
  - sqlite 全程序 vmp：174 个中层函数虚拟化，编译 156s（**46x**）；
  - 全部回退**无任何提示**，使用者无法得知实际保护了什么。
- 建议：pass 结束时输出统计（虚拟化 N / 回退 M + 回退原因分布），或经
  OptimizationRemark 输出 summary；评估回退路径是否可避免污染优化。

## V-6 自动向量化 IR 静默回退（insertelement）

- 严重度：低；状态：可用 `-fno-vectorize -fno-slp-vectorize` 规避
- 现象：`crc32_word` 在默认 `-O2` 下被向量化（insertelement），VMP 回退
  且无提示；加 `-fno-vectorize -fno-slp-vectorize` 后准入（随后触发 V-2）。
- 建议：文档注明与自动向量化的互斥；或准入检测到向量 IR 时输出 remark。

## 2026-10-20 更新：M3 指令覆盖扩展（GEP / 数组 alloca / volatile / 全局地址 / 内存 intrinsic）

- GEP 在资格检查前预分解为整数地址运算（常量折叠 + 动态下标缩放），
  常量表达式指针运算（含 PHI incoming）先物化再分解；
- 数组/标量静态 alloca 进入 VM 帧（Direct CodeGen 布局本就支持，
  此前被准入门误挡）；
- volatile 标量访存放行（VM 按程序序逐次真实访存，单线程语义等价；
  atomic 仍拒绝）；
- 全局地址经值表补丁槽：编译期占位 0，包装函数调用解释器前回填；
- `llvm.memcpy/memmove/memset` 改写 libc 直调（HOSTCALL），
  `llvm.objectsize` 折叠 -1。
- 结果：L2 的 7 个标准算法顶层函数全部可虚拟化（此前 7/7 回退）；
  开销量级见性能报告「VMP × 7 算法」节。

## 附：关联问题（vmfla 链路，非 VMP 本体）

以下来自同一轮测试，属 vmfla/组合链路，记录备查（详见性能报告）。
两项均已于后续修复：

- `luaV_execute`（lua 解释器主循环巨型函数）单独标注 vmfla 即 SIGSEGV，
  `lvm.c` 其余 31 个函数正常——2026-10-16 修复（indirectbr 直连边破坏
  状态机不变式，case 头部写回自身表下标，缺陷 #4）；
- `ibr + vmfla` 组合在全部 4 应用上进程启动即静默退出（argv 破坏征兆），
  单 pass 与其余两两组合均正常——2026-10-20 修复（调度改为 vmfla 先行、
  ibr 延迟到晚期分发，缺陷 #3）。

## 可用数据基线（修复后的回归参考）

- 可完整测量的 VMP 开销：整函数 **142~579x**（k_loop 142x、k_leaf 186x、
  k_branch 291x、fib-iterative-n92 566~579x），约 **30ns/虚拟指令**、
  hostcall 往返 **144ns**（Apple M4 Pro，-O2）；
- zlib 薄包装（`adler32`/`crc32`）应用级 ≈ 1.0x（内部循环原生）；
- 复测命令：`python3 test/perf/vmp_fn_bench.py`（结果直接打印）。
