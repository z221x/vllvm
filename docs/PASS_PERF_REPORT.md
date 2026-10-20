# VLLVM Pass 开源应用性能测试报告

- 测试窗口：2026-10-09 ~ 2026-10-10（三轮：初测 → 合并远程重构复测 → enstr 修复后终测；**本文数据以终测为准**，沿革见文末）
- 测试机：Apple M4 Pro（arm64，14 核，48GB），macOS 27.0
- 工具链：VLLVM clang 21.1.0（`build/llvm-macos`；含目录重构 + VLLVMConfig 等级系统 + enstr mmap 池 + 本轮两项跨 TU 修复；链接用系统 `/usr/bin/ld`，见「环境说明」）
- 原始数据：全部呈现在本文表格中；复测后可用 `test/perf/gen_report.py` 重新生成

## 结论速览

以「全程序函数标注」（每个 TU 的所有函数打标注）在 4 个开源 C 项目上逐一启用各 Pass：

| 维度 | 结论 |
| --- | --- |
| 运行时开销 | `enstr` ≈ **1.0x**（近零）；`merge` **1.4~4.3x**；`bb2func` **2.8~6.1x**；`bcf` **3.7~6.0x**；`icall` **2.8~6.5x**（L1/L2 持平）；`ibr` **5.4~8.8x**；`fla` **13.2~30.3x**；`vmfla` **6.7~20.1x**（2026-10-16 分桶路由 v2 后；二叉树时代 19.5~30.3x）；`bcf+vmfla` 组合 **17.9~35.7x** |
| 二进制体积 | `bcf` 最大（**8~10x**）；`vmfla` **3.1~5.5x**（分桶路由后；二叉树时代 7~15x）；`fla`/`ibr` 约 **3~5x**；`icall` **1.4~2.1x**；`enstr`/`vmp` ≈ 1.0~1.4x |
| 编译耗时 | 除 `vmp`（sqlite **156s ≈ 46x**）外均在基线 3 倍以内 |
| 功能正确性 | 累计发现 **7 个缺陷**，其中 **6 个已修复**（icall 跨 TU、enstr 跨 TU、vmfla 崩溃于 `luaV_execute`、ibr+vmfla 组合反序执行、bcf 多 TU 重定义、vmp 静态函数 ODR）；**仍遗留 1 个**：VMP 问题清单 |
| vmp 实际效果 | 全程序标注下热点基本回退：bzip2/zlib 仅冷函数/薄包装被虚拟化（≈1.0x）；sqlite 虚拟化 174 个中层函数（4.0x）；lua 全回退但仍 1.5x（回退路径阻塞优化） |

**工程选型顺序**（开销从低到高）：enstr < merge < bb2func ≈ bcf < icall < ibr < fla < vmfla。
`enstr` 可全开；`fla`/`vmfla` 只用于非热路径函数；`icall` 参数加密只对内部链接
（`static`）目标生效，跨 TU 可调的函数仅入池间接化（安全语义）；`vmp` 只适合
低频调用的关键算法。详见「VMP 函数级开销」。

## 被测应用与负载

| 应用 | 版本/规模 | 负载（base 中位耗时） |
| --- | --- | --- |
| bzip2 | 1.0.8，8 个 TU | `-9` 压缩 + 解压 20MB 文本/随机混合数据，md5 校验（~2.0s） |
| zlib | 1.3.1，16 个 TU（zbench 驱动） | compress2/uncompress 32MB × 4 轮，crc32 往返校验（~1.8s） |
| lua | 5.4.7，33 个 TU | n-body 150 万步 + 字符串/表操作脚本（~1.2s） |
| sqlite | 3.45.1 amalgamation，2 个 TU | `:memory:` 库 120 万行建库/索引/聚合 + 300 万次递归 CTE（~1.6s） |

每个配置：1 次预热 + 6 次计时取中位数；正确性 = 输出与基线**逐字节一致**且退出码为 0。
默认等级（icall/enstr = level 1）；等级 2 经 `VLLVM_CONFIG=icall=2` 环境变量专项测量。

## 测试方法

1. **两段式构建**：`clang -O0 -g0 -S -emit-llvm -Xclang -disable-O0-optnone` 产出每个 TU 的原始 IR；用 `test/perf/inject_vllvm_attrs.py` 对每个 `define` 注入 `"vllvm.<kind>"` 函数属性（等价源码 `annotate("vllvm:...")`）；再 `clang -O2 -g0 -c` 编译链接。VLLVM Pass 挂在 pipeline start/相应扩展点，与源码直编同一时点触发。`VLLVM_CONFIG` 环境变量转发 `-mllvm -vllvm-config=<pass>=<level>`。
2. **等价性验证**：两段式 base 与单段式 `-O2` 直编输出完全一致，性能偏差约 7%（缺 lifetime intrinsic）；所有配置同用两段式，比值内部自洽。
3. IR 注入保证 100% 函数被标注，避免对开源源码做脆弱正则改写。
4. **环境问题**：自建 clang 默认的 `ld64.lld` 无法解析 macOS 27 SDK 的 tbd（`arm64e.x1-macos` 等新架构标识）。本报告所有链接统一 `-fuse-ld=/usr/bin/ld`；仓库自带 shell 测试在本机需 `CLANG=<加 -fuse-ld 的包装>` 运行（与 Pass 无关）。

## 最终运行时数据（enstr 修复后，2026-10-10）

### bzip2

| 配置 | 运行中位 (s) | vs base | 体积 (strip) | 体积比 | 编译耗时 (s) | 状态 |
| --- | --- | --- | --- | --- | --- | --- |
| base | 2.046 | 1.00x | 119240 | 1.00x | 0.18 | ✅ |
| fla | 27.008 | 13.20x | 400120 | 3.36x | 0.08 | ✅ |
| bcf | 7.650 | 3.74x | 1011000 | 8.48x | 0.13 | ✅ |
| ibr | 11.062 | 5.41x | 367000 | 3.08x | 0.06 | ✅ |
| icall | 5.982 | 2.92x | 169176 | 1.42x | 0.13 | ✅ |
| vmfla | 26.449 | 12.93x | 846088 | 7.10x | 0.19 | ✅ |
| enstr | 2.077 | 1.02x | 119304 | 1.00x | 0.25 | ✅ |
| bb2func | 5.698 | 2.79x | 202632 | 1.70x | 0.07 | ✅ |
| merge | 5.470 | 2.67x | 168616 | 1.41x | 0.05 | ✅ |
| vmp | 2.133 | 1.04x | 119304 | 1.00x | 0.19 | （7 个冷函数虚拟化） |
| combo2 | - | - | - | - | - | ❌ |
| icall(L2,加密+诱饵) | 5.774 | 2.76x | 169176 | 1.42x | 0.06 | ✅ |

### zlib

| 配置 | 运行中位 (s) | vs base | 体积 (strip) | 体积比 | 编译耗时 (s) | 状态 |
| --- | --- | --- | --- | --- | --- | --- |
| base | 1.764 | 1.00x | 85672 | 1.00x | 0.17 | ✅ |
| fla | 34.192 | 19.39x | 383112 | 4.47x | 0.11 | ✅ |
| bcf | 9.925 | 5.63x | 911416 | 10.64x | 0.23 | ✅ |
| ibr | 11.786 | 6.68x | 349976 | 4.09x | 0.16 | ✅ |
| icall | 8.635 | 4.90x | 135528 | 1.58x | 0.08 | ✅ |
| vmfla | 50.051 | 28.38x | 961272 | 11.22x | 0.24 | ✅ |
| enstr | 1.821 | 1.03x | 118744 | 1.39x | 0.18 | ✅ |
| bb2func | 10.751 | 6.10x | 169256 | 1.98x | 0.10 | ✅ |
| merge | 7.601 | 4.31x | 135176 | 1.58x | 0.15 | ✅ |
| vmp | 1.773 | 1.01x | 102232 | 1.19x | 0.30 | （仅薄包装虚拟化） |
| combo2 | - | - | - | - | - | ❌ |
| icall(L2,加密+诱饵) | 8.621 | 4.84x | 135528 | 1.58x | 0.07 | ✅ |

### lua

| 配置 | 运行中位 (s) | vs base | 体积 (strip) | 体积比 | 编译耗时 (s) | 状态 |
| --- | --- | --- | --- | --- | --- | --- |
| base | 1.228 | 1.00x | 242792 | 1.00x | 0.34 | ✅ |
| fla | 36.092 | 29.39x | 1020184 | 4.20x | 0.31 | ✅ |
| bcf | 7.370 | 6.00x | 2472632 | 10.18x | 0.48 | ✅ |
| ibr | 10.748 | 8.75x | 870712 | 3.59x | 0.29 | ✅ |
| icall | 6.036 | 4.92x | 443144 | 1.83x | 0.26 | ✅ |
| vmfla | - | - | - | - | - | ❌（luaV_execute 崩溃） |
| enstr | 1.218 | 0.99x | 259400 | 1.07x | 0.40 | ✅ |
| bb2func | 5.126 | 4.17x | 510280 | 2.10x | 0.27 | ✅ |
| merge | 1.716 | 1.40x | 374120 | 1.54x | 0.29 | ✅ |
| vmp | 1.815 | 1.48x | 293384 | 1.21x | 0.54 | （全回退但降优） |
| combo2 | - | - | - | - | - | ❌ |
| icall(L2,加密+诱饵) | 6.104 | 4.91x | 443144 | 1.83x | 0.22 | ✅ |

### sqlite

| 配置 | 运行中位 (s) | vs base | 体积 (strip) | 体积比 | 编译耗时 (s) | 状态 |
| --- | --- | --- | --- | --- | --- | --- |
| base | 1.615 | 1.00x | 924440 | 1.00x | 3.80 | ✅ |
| fla | 48.912 | 30.29x | 4725800 | 5.11x | 3.47 | ✅ |
| bcf | 7.063 | 4.37x | 13574360 | 14.68x | 5.27 | ✅ |
| ibr | 9.951 | 6.16x | 4459064 | 4.82x | 2.26 | ✅ |
| icall | 10.557 | 6.54x | 1911944 | 2.07x | 2.28 | ✅ |
| vmfla | 91.000 | 56.35x | 13070584 | 14.14x | 9.84 | ✅ |
| enstr | 1.785 | 1.11x | 973960 | 1.05x | 4.11 | ✅ |
| bb2func | 5.521 | 3.42x | 2044360 | 2.21x | 2.78 | ✅ |
| merge | 4.403 | 2.73x | 1615112 | 1.75x | 1.69 | ✅ |
| vmp | 6.493 | 4.02x | 992488 | 1.07x | 181.02 | （174 个函数虚拟化，编译 46x） |
| combo2 | - | - | - | - | - | ❌ |
| icall(L2,加密+诱饵) | 10.236 | 6.21x | 1911944 | 2.07x | 2.21 | ✅ |

### 汇总（运行时 / 体积，vs base）

| Pass | bzip2 | zlib | lua | sqlite | 几何平均 |
| --- | --- | --- | --- | --- | --- |
| fla | 13.2x | 19.4x | 29.4x | 30.3x | 21.8x |
| bcf | 3.7x | 5.6x | 6.0x | 4.4x | 4.8x |
| ibr | 5.4x | 6.7x | 8.8x | 6.2x | 6.6x |
| icall | 2.9x | 4.9x | 4.9x | 6.5x | 4.6x |
| vmfla | 12.9x | 28.4x | ❌ | 56.3x | 27.4x |
| enstr | 1.0x | 1.0x | 1.0x | 1.1x | 1.0x |
| bb2func | 2.8x | 6.1x | 4.2x | 3.4x | 3.9x |
| merge | 2.7x | 4.3x | 1.4x | 2.7x | 2.6x |
| vmp | 1.0x | 1.0x | 1.5x | 4.0x | 1.6x |
| combo2 | ❌ | ❌ | ❌ | ❌ | - |

| Pass | bzip2 | zlib | lua | sqlite | 几何平均 |
| --- | --- | --- | --- | --- | --- |
| fla | 3.4x | 4.5x | 4.2x | 5.1x | 4.2x |
| bcf | 8.5x | 10.6x | 10.2x | 14.7x | 10.8x |
| ibr | 3.1x | 4.1x | 3.6x | 4.8x | 3.8x |
| icall | 1.4x | 1.6x | 1.8x | 2.1x | 1.7x |
| vmfla | 7.1x | 11.2x | ❌ | 14.1x | 10.4x |
| enstr | 1.0x | 1.4x | 1.1x | 1.1x | 1.1x |
| bb2func | 1.7x | 2.0x | 2.1x | 2.2x | 2.0x |
| merge | 1.4x | 1.6x | 1.5x | 1.7x | 1.6x |
| vmp | 1.0x | 1.2x | 1.2x | 1.1x | 1.1x |
| combo2 | ❌ | ❌ | ❌ | ❌ | - |

## icall 等级语义

`-mllvm -vllvm-config=icall=<n>`：0 关闭；**1（默认）**仅专属跳板间接化
（icallcc + `mov w19,#调用号` + naked 跳板内 `ubfx/and` 解码查池 `ret x19`）；
**2** 再叠加参数加密（x0~x7 上的 32/64 位整数与指针，仅 add/sub/xor 可逆链，
入口左值解密）与 fake 诱饵下标。**L1 与 L2 开销基本持平**（2.8~6.5x；
加密链在跳板汇编内执行，对热路径增量有限）——默认开 L2 即可。

## 缺陷记录与修复

| # | 缺陷 | 根因 | 状态 |
| --- | --- | --- | --- |
| 1 | icall：跨 TU 调用参数加密错乱（崩溃/静默错数据） | module pass 只能改写本模块调用点；被调方 external linkage 时其他 TU 明文直调命中解密入口 | ✅ 已修复（非 local linkage 不加密，仅入池） |
| 2 | enstr：zlib 崩溃/SIGBUS 堆破坏 | zlib `deflate.h` 把 `_tr_tally` 做成内联宏，deflate.c 跨 TU 直接读被密文化的 `@_dist_code`（external linkage）→ `d_code()` 返回密文乱值 → `dyn_dtree[乱值].Freq++` 越界写坏堆（`desc->dyn_tree` 指针被加 `0x2_0000_0000`） | ✅ 已修复（同 #1 思路：非 local linkage 不进池） |
| 3 | ibr + vmfla 组合：静默错结果（2026-10-10 表现为启动即退出；2026-10-20 最小复现为函数体不执行、原样返回入口值，如 `mix(7)` 直接返回 7） | 旧调度反序执行：ibr 在函数分发早期先改写，vmfla 晚期再面对已被改写成 indirectbr 的 CFG——地址表跳转网络被当孤岛排除在状态机外（case 常量表仅含 return case），而入口终结指令仍被改写为直连 dispatcher，循环/分支体整体不执行 | ✅ 已修复（2026-10-20：同函数两者都标注时 ibr 延迟到晚期分发、紧随 vmfla 执行并作用于 `<fn>.vllvm.impl` 平坦化产物，`PassInfo.cpp` `IbrLateAttr`；回归 `test/complex/test_pair_combos.sh` 逐对冒烟 28/28） |
| 4 | vmfla 崩溃于 `luaV_execute`（lua 解释器主循环，数千行巨型函数；lvm.c 其余 31 函数正常） | computed goto 的 `indirectbr` 直连边绕过 dispatcher，破坏增量状态机的"进入 case 时下标 == 该 case 表下标"不变式 | ✅ 已修复（非常规前驱的 case 头部写回自身表下标；回归 `test/vmfla/test_vmfla_indirectbr.sh`，详见 2026-10-16 节） |
| 5 | VMP 的 6 个问题（递归崩溃、CRC 形态 VM 断言、哈希类不可表达等） | 见 `docs/VMP_ISSUES.md` | ❌ 遗留（记录在案） |
| 6 | bcf：多 TU 各自标注时链接 `duplicate symbol _export_write`（L3 首轮 4 应用 × bcf/bcfvmfla 共 8 配置全部 build-failed） | seed 收敛为模块级共享导出变量后，每个 TU 的模块各自携带强符号定义 | ✅ 已修复（定义改 WeakAnyLinkage，各 TU 弱定义、链接器合并为全程序同一变量；回归 `test/boguscontrolflow/test_bcf_multi_tu.sh`，详见 2026-10-20 节） |
| 7 | vmp：静态函数被虚拟化后以外部符号链接，与其他 TU 同名函数 `duplicate symbol`（bzip2/vmp：bzlib.c 的 static `myfeas` 对 bzip2.c 的外部 `myfeas`） | `installWrapper` 的 `F.deleteBody()` 把函数转声明时 LLVM 会把 linkage 重置为 External，wrapper 装回后仍为外部链接 | ✅ 已修复（`deleteBody` 前保存、后恢复原 linkage；回归 `test/vmp/test_vmp_static_dup.sh`，详见 2026-10-20 节） |
| 8 | enstr+icall 组合：应用级 sqlite SIGBUS（输出完整后崩溃，rc=138）；最小复现 = 仅对 shell.c `process_input` 单函数施加 enstr+icall | icallcc 调用方代码生成缺陷：nest 参数写入 X19 与调用方在 X19 上的活跃值（如 Shell 结构体指针）冲突。IR 层两配置完全一致（`call icallcc … i32 nest 0`），icall 单独"通过"是寄存器分配运气（低压力下 RA 不占用 X19 跨调用）；enstr 全模块字符串加密在未标注调用方插入池调用抬高压力后暴露。GlobalIsel 开/关均复现（SelectionDAG 与 GlobalISel 公共路径）。修复方向：nest 改经 X16/X17（IP scratch，天然 caller-saved，天然无冲突）或修正调用方 regmask 使 X19 被视为跨 icallcc 调用破坏 | ❌ 遗留（记录在案；L2 算法级 combo1 未触发（压力不足），应用级 combo1 = 3/4 应用可用，sqlite 受阻） |

### 缺陷 2 详录（enstr 跨 TU，2026-10-10 反汇编逆向定位）

**证据链**：链接二进制中没有任何 `bl __tr_tally`（zlib 1.3.1 将其内联进
deflate.c）→ `__vllvm_enstr_get` 断点从未命中 → 池表/密文/运行时单独复放全部
正确 → 真正的破坏者是**未标注 TU 的内联宏读到密文**。表现高度隐蔽：文本数据
偶发通过、混合数据必崩、`-O0/-O1/-O2` 全级别复现、崩溃点（`build_tree` SIGBUS）
远离病因（deflate.c 的越界 `Freq++`）。

**修复后验证**：TU 二分 16/16 通过；20MB 混合数据压缩/解压 CRC 正确；全量回归
22/23；全矩阵 zlib/enstr 由失败转为 1.03x。**经验**：module pass 对
external linkage 目标做「改数据 + 改本模块引用」的变换时，必须假设其他 TU
持有明文引用——icall 与 enstr 两例同构，后续 pass 设计需引以为鉴。

## VMP 函数级开销

对 zlib/bzip2/lua 逐个函数单独标注 `vllvm:vmp` 探测准入（`.o` 中
`__vllvm_vmp_code.<fn>` 符号），对通过者微基准；另构造合成准入形态内核
（`test/perf/vmp_fn_bench.py`）。VMP 缺陷清单见 `docs/VMP_ISSUES.md`。

### 准入形态内核

| 内核 | 形态 | base | vmp | 开销 | 单位成本 |
| --- | --- | --- | --- | --- | --- |
| k_leaf | 10 条算术 + 1 次 load | 1.3ns | 241ns | **186x** | 241 ns/call |
| k_loop | 紧凑整数循环 | 1.0ns | 134ns | **147x** | 134 ns/iter |
| k_branch | if/else + switch 链 | 1.3ns | 409ns | **291x** | 409 ns/call |
| k_mem | 循环写内存 | - | - | **回退** | 不准入 |
| k_call | hostcall 原生函数 | 1.0ns | 143ns | **150x** | 143 ns/次往返 |

**每条虚拟指令约 20~45ns**（M4 Pro），分支类最贵；hostcall 往返 143ns。

### 标准算法矩阵（10 项）

按「保护核心原语」分解实现（哈希压缩块/逐字 CRC；标量化：无数组、无 GEP，
官方测试向量校验通过）：

| 算法 | 准入 | 虚拟化后 | 开销 |
| --- | --- | --- | --- |
| fib-iterative-n92 | ✅ | 正确 | **566~579x**（22.7ns→12.8µs，与 ~30ns/指令线性自洽） |
| fib-recursive-n32 | ✅ | **SIGSEGV** | hostcall 桥不支持自递归 |
| crc32-64b/4kb | ✅ | **SIGTRAP** | EXC_BREAKPOINT 落在 `__vllvm_vmp_execute`（解释器）内部（VM 断言）；最小复现 `test/perf/repro/vmp_crc32_trap.c` |
| sha256/sha1/md5（64b/4kb） | ❌ 全部回退 | - | 旋转被 instcombine 融合成 fshl/fshr（M3 不支持）；volatile 屏障被拒；16 消息字超 R0-R5 六参数上限；-O0 全局读报 SSA 限制 |

**结论**：可测 VMP 开销为整函数 **147~579x**；只适合低频调用的关键算法。
当前准入排除面大（GEP/写内存/向量 IR），真实项目热点全部回退。

### 项目真实函数（单函数标注探测）

| 函数 | 准入 | 开销 | 说明 |
| --- | --- | --- | --- |
| zlib `adler32` / `crc32` | ✅ | ≈1.0x | 仅薄包装虚拟化，循环在被调原生函数内（保护收益≈0） |
| zlib `crc32_z`/`deflate`/`_tr_flush_block`、bzip2 `BZ2_hb*`、lua `luaV_idiv` 等 | ❌ | - | 热点均不通过准入 |

## 字符串加密（enstr）效果示例

bzip2 真实二进制：可读字符串 346→170 条，错误提示/状态消息（"bucket
sorting ..."、"reconstructing block ..." 等）全部消失，替代为
`l_vllvm.enstr.cipher` 密文 blob（逐串独立密钥流异或 + 槽间随机填充）+ 运行时
匿名 mmap 页按需解密；压缩输出 md5 与基线一致，开销 1.02x。

保护范围（修复后语义）：本 TU 内 **local linkage** 的只读 i8 数组/字符串
（字面量、`static const char[]`）；外部可见目标保持明文（跨 TU 引用无法改写）。
level 2（`enstr=2`）额外加密函数体内的大整数常量（密文表 + volatile 查表）。

## 2026-10-12 复测：独立 BCF 新算法 + vmfla 两级等级

工具链变化（其余方法与本报告一致）：独立 `FlattenFuncPass` 删除（`fla`
标注归并进 vmfla 链路，效果等同 vmfla level 1）；`BogusControlFlowPass`
以新算法独立重做（栈 seed + 随机运算链加密 + 单后继分支永假谓词，真实块/
虚假块随机作虚假后继）；vmfla 收缩为两级（1=bb2func+merge+平坦化，
2=+局部变量结构化），本轮默认 level 1（不含 lvars/BCF）。
原始数据：`tmp/perfbench/full-v2/results.json`。

### 运行时（vs base）

| Pass | bzip2 | zlib | lua | sqlite | 几何平均 |
| --- | --- | --- | --- | --- | --- |
| fla（=vmfla L1） | 7.85x | 17.55x | ❌ | 31.33x | 16.28x |
| bcf（新算法） | 3.01x | 4.85x | 3.75x | 2.87x | **3.54x** |
| ibr | 5.72x | 9.52x | 8.58x | 6.56x | 7.44x |
| icall | 3.02x | 4.96x | 4.83x | 6.19x | 4.60x |
| vmfla | 19.34x | 17.23x | ❌ | 31.45x | 21.88x |
| enstr | 1.09x | 1.00x | 0.98x | 1.09x | 1.04x |
| bb2func | 2.87x | 6.52x | 4.11x | 3.24x | 3.97x |
| merge | 2.80x | 4.16x | 1.38x | 2.73x | 2.57x |
| vmp | 1.07x | 0.99x | 1.53x | 4.36x | 1.63x |
| combo/combo2 | ❌ | ❌ | ❌ | ❌（combo2 sqlite 编译 >1800s 超时） | - |

### 要点

- **新 BCF 比旧版全局更轻**：运行时 2.9~4.9x（旧 3.7~6.0x），体积
  几何平均 2.38x（旧 7~15x）——旧版每个命中块都复制整段 fake 分支与
  不透明谓词全局，新版只在单后继分支前插一条密文链 + 永假比较，虚假块
  仅含随机 junk 指令。
- **fla 与 vmfla 数据完全一致**（体积逐字节相同），验证别名归并生效；
  lua/fla 与 lua/vmfla 同样命中既有 `luaV_execute` 崩溃（fla 现在就是
  vmfla 链路，两行实为同一配置）。
- **vmfla level 1 相比旧默认（lvars 常开）**：bzip2 19.3x（旧 12.9x，
  但旧值含 lvars 的 malloc/解密开销）、sqlite 31.5x（旧 56.3x）——
  lvars 移出默认档后 sqlite 明显回落；bzip2 波动来自本轮 RNG 与块序
  差异。
- combo 组合仍失败（既有 ibr+vmfla 组合问题）；sqlite 上全 pass 叠加
  编译超 30 分钟被超时记录，属编译开销而非正确性。

## 2026-10-13 复测：BCF 前置处理（大块切割 + 虚假块 + 全块重排）

工具链变化：独立 BCF 增加前置处理——真实块大块切割（≥6 条指令切成
2~4 条碎片，碎片间无条件 br 成为新改写点）、随机指令虚假块、全块重排；
切割产生的碎片间跨块 SSA 经 `fixStackForFlatten` 降栈（修复支配关系
破坏导致的 IRTranslator 崩溃）；加密链去掉乘法只用 add/sub/xor。
其余方法与本报告一致；原始数据 `tmp/perfbench/full-v3/results.json`。

### BCF 前后对比（vs base）

| 指标 | 旧 BCF（v2） | 新 BCF（v3） |
| --- | --- | --- |
| 运行时几何平均 | 3.54x | **8.65x**（6.3/12.1/11.9/6.2x） |
| 体积几何平均 | ~2.4x | **~6.4x**（5.4/7.2/6.2/6.7x） |
| 编译耗时 | ~1x | ~1x（sqlite 2.9s vs base 3.4s） |
| 正确性 | 4/4 | **4/4**（输出逐字节一致） |

前置处理是开销主因：切割使单后继分支基数扩大数倍，默认 level 1（60%
覆盖率）改写的谓词数量随之翻倍以上；降栈为每个跨块值增加 load/store。
混淆强度与开销同步上升属预期，对热路径敏感的场景建议控制标注范围。

### 全配置运行时（vs base，v3）

| Pass | bzip2 | zlib | lua | sqlite | 几何平均 |
| --- | --- | --- | --- | --- | --- |
| bcf | 6.29x | 12.10x | 11.85x | 6.20x | 8.65x |
| fla | 17.59x | 15.83x | ❌ | 29.77x | 20.24x |
| vmfla | 19.52x | 27.61x | ❌ | 30.32x | 25.38x |
| ibr | 5.42x | 7.90x | 8.23x | 6.29x | 6.86x |
| icall | 2.85x | 4.80x | 4.83x | 6.12x | 4.49x |
| enstr | 1.02x | 1.03x | 0.99x | 1.09x | 1.03x |
| bb2func | 4.18x | 6.26x | 4.16x | 3.34x | 4.37x |
| merge | 2.70x | 4.41x | 1.36x | 2.71x | 2.57x |
| vmp | 1.02x | 1.01x | 1.54x | 4.27x | 1.61x |

- 非变体 Pass（ibr/icall/enstr/merge/vmp）与 v2 一致，vmfla 代码
  无改动；fla/vmfla 两轮数值波动偏大（zlib 上两轮产物逐字节相同、
  中位却 30.3s vs 46.8s，同轮内 fla 与 vmfla 完全相同产物也有
  26.8s vs 46.8s），重负载配置的运行时受机器状态影响明显，横向比值
  以同轮内为准。
- 遗留失败不变：lua fla/vmfla（luaV_execute 崩溃）、combo/combo2
  （ibr+vmfla 组合静默失败）。本轮 sqlite/combo2 编译未再超时（组合
  大量回退，产物仅 221KB）。

## 2026-10-14 复测：切割阈值重定义（≥8 条、段长≥4）

应用户要求调整大块切割定义：有效指令数阈值 6→8，切割段长由 2~4 改为
4~6（每段至少 4 条）。其余不变；本轮仅测 base+bcf（其余 Pass 产物不
受影响），原始数据 `tmp/perfbench/bcf-v4/results.json`。

### BCF 对比（vs base，level 1）

| 指标 | v3（≥6/2~4） | v4（≥8/≥4） |
| --- | --- | --- |
| 运行时几何平均 | 8.65x | **6.68x** |
| 分应用 | 6.3/12.1/11.9/6.2x | 5.1/9.3/8.2/5.2x |
| 体积几何平均 | ~6.4x | ~4.7x |
| 正确性 | 4/4 | **4/4** |

切割更保守后碎片数下降（fixture 块数 34→25、L1 谓词 17→9），运行时
回落约 23%、体积回落约 26%，仍显著高于无切割版本（3.54x）——碎片化
CFG 与谓词密度换混淆面的基本盘不变。

## 2026-10-14 复测：bcf + vmfla 组合

新增 `bcfvmfla` 配置（`vllvm.bcf` + `vllvm.vmfla` 同函数标注）；同轮内
并列测 bcf / vmfla 单独档以消除轮间机器波动。调度路径：vmfla 链路先
bb2func+merge，bcf 作用于切分后的 wrapper，vmfla 在链路末端平坦化含
虚假分支的 CFG。原始数据 `tmp/perfbench/bcfvmfla/results.json`。

### 运行时（vs base，同轮，level 均为默认 1）

| 应用 | bcf | vmfla | bcf+vmfla | 叠加比（组合/(bcf+vmfla-base)） |
| --- | --- | --- | --- | --- |
| bzip2 | 4.38x | 16.76x | 15.66x | 0.78 |
| zlib | 9.38x | 15.54x | 21.36x | 0.89 |
| sqlite | 4.89x | 31.83x | **52.68x** | 1.47 |
| lua | 8.30x | ❌ | ❌（继承 luaV_execute 崩溃） | - |
| 几何平均 | 6.39x | 20.24x | 26.02x | - |

体积：组合约为单独档之和的量级（bzip2 5.4x / zlib 7.8x / sqlite 9.5x）。
正确性：3/3 可测应用输出逐字节一致。

要点：平坦化状态下 vmfla 的分发循环每轮都要穿过 bcf 谓词链时开销
相乘（sqlite 1.47 倍于线性叠加）；bzip2/zlib 上 vmfla 占主导、bcf 的
边缘被合并掉一部分（0.78~0.89）。组合适合只标注少量高价值函数。

## 2026-10-14 复测：BCF 当前形态（单次加密 + 克隆虚假块 + 全局 seed）

在 v4（≥8/段≥4 切割）基础上再叠加：每个改写点只做一次 add/sub/xor
（原先 3~6 次）；虚假块指令改为克隆真实块指令（保留值名/调试信息，
跨块操作数替换为常量/seed 槽）；seed 由栈槽切换为每函数私有全局变量
（入口以全局地址经恒等式归一后 volatile 写入）。仅测 base+bcf，原始
数据 `tmp/perfbench/bcf-v6/results.json`。

### BCF 对比（vs base，level 1）

| 应用 | v4 | v6（当前） | v6 体积 | v4 体积 |
| --- | --- | --- | --- | --- |
| bzip2 | 5.06x | 4.09x | 2.94x | 4.05x |
| zlib | 9.28x | 8.11x | 3.70x | 5.24x |
| lua | 8.15x | 7.67x | 3.31x | 4.74x |
| sqlite | 5.21x | 4.38x | 3.61x | 4.95x |
| 几何平均 | 6.68x | **5.78x** | ~3.4x | ~4.7x |

正确性 4/4。单次加密与克隆虚假块使运行时回落约 13%、体积回落约 27%
（每点省 2~5 条冗余运算；克隆指令比纯随机链更短）；全局 seed 与栈槽
开销相当（同为一次 volatile 访存）。

## 2026-10-15 对比测试：amice（开源 LLVM Pass 插件）控制流平坦化开销

被测对象：[amice](https://github.com/fuqiuluo/amice) v0.1.5-beta.3 预编译
插件（`libamice-llvm21-macos-arm64.dylib`）+ Homebrew 原版 LLVM 21 clang，
`-fpass-plugin` 接入，`AMICE_FLATTEN=true` 全局开启（等价我们的"全程序
标注"口径），basic / dominator 两模式；同一套 4 应用与负载、同机同轮。
原始数据 `/tmp/amice-bench/out/results.json`（Runner 在 `/tmp/amice-bench/run_amice.py`，
复用本仓库 test/perf 的应用定义）。

### 运行时与体积（vs 同编译器 base）

| 应用 | amice basic | amice dominator | 体积 basic/dom | VLLVM vmfla L1（对照） |
| --- | --- | --- | --- | --- |
| bzip2 | 5.83x | 9.16x | 2.1x / 5.4x | 19.5x |
| zlib | 6.87x | 11.62x | 2.5x / 6.3x | 27.6x |
| lua | 14.35x | 21.29x | 2.2x / 4.5x | 28.9x（2026-10-16 修复后） |
| sqlite | 8.84x | 57.60x | 2.9x / 9.1x | 30.3x |
| 几何平均 | **8.44x** | **19.01x** | - | 26.2x（4 应用；lua 修复前为 25.4x/3 应用） |

amice 全部 12 个组合输出与基线一致。编译耗时：sqlite 上 basic 190s /
dominator 207s（amice 插件较慢）；bzip2 3.7s/7.4s。

### 结论

- amice basic 模式约 8.4x，明显轻于 VLLVM vmfla（26.2x）——vmfla 是
  bb2func+merge+平坦化三段链且带常量表/防传播机制，单"平坦化"维度
  上 amice basic 与其体积比（2~3x vs 5x）都更保守。
- amice dominator（19.0x）与 vmfla 同量级：sqlite 上 57.6x 略高于
  vmfla 的 30.3x。
- lua 上两者均可完整跑通（VLLVM vmfla 的 luaV_execute 崩溃已于
  2026-10-16 修复，见下节）。
- 跨工具对比含编译器差异（原版 LLVM 21 vs VLLVM 21.1），比值均以
  各自同编译器 base 为参照，方向性结论可信。

## 2026-10-16 修复：vmfla 崩溃于 luaV_execute（indirectbr 破坏状态机不变式）

### 根因

`luaV_execute` 是 computed goto 解释器：静态表 `disptab` 存 83 个
`blockaddress`，dispatch 块以 `indirectbr` 直连各 opcode case 块。vmfla
平坦化把 case 间跳转改写成**增量状态机**（`新下标 = 当前下标 + 常量增量`，
再回 dispatcher 按 case 值路由），依赖不变式"进入 case 时运行时下标 ==
该 case 的表下标"。`indirectbr` 块被排除在状态机外（终结符不可重写），
其直连边**绕过 dispatcher** 进入 case：运行时下标仍是旧值，增量算出
错误状态——dispatcher 按 case 值匹配失败落入 `switchDefault` 死循环
（full-v3 观察到的 run-timeout/100% CPU），或恰好命中错误 case 破坏
数据（SIGSEGV）。单独标注 `luaV_execute` 即可复现。

### 修复

`applyFlatten` 在终结符重写后检测仍有"非常规前驱"（非调度器生成块：
`indirectbr`、EH 孤岛等）的 case，在 case 头部写回自身表下标
（volatile store），重建不变式；lvars 的 `loadConstViaFlattenIndex`
锚定下标读取同样依赖该不变式。函数体/表参数化等其他机制无需改动
（`moveTablesToImplParams` 对 indirectbr 本就整体回退）。
回归：`./test/vmfla/test_vmfla_indirectbr.sh`（computed goto fixture，
修复前死循环、修复后通过，含 IR 断言）。

### 结果（lua，33 TU 全标注）

| 配置 | 运行中位 (s) | vs base | 体积 (strip) | __text | 状态 |
| --- | --- | --- | --- | --- | --- |
| base | 1.196 | 1.00x | 242792 | 196608 | ✅ |
| vmfla L1 | 34.505 | **28.9x** | 1286280 | 1196032（6.1x） | ✅ 6/6 轮，输出与 base 逐字节一致 |

vmfla 四应用几何平均更新为 **26.21x**（bzip2 19.5x / zlib 27.6x /
lua 28.9x / sqlite 30.3x）。与 amice（lua basic 14.35x / dominator
21.29x）同量级偏重：解释器 dispatch 循环平坦化后每条 VM 指令多一次
dispatcher 往返 + 旁路入口的索引写回。combo 中 ibr+vmfla 组合缺陷
（缺陷 3）不受影响，仍遗留。

## 2026-10-16 优化：dispatcher 分桶路由（跳转表 + 桶内比较链）

### 动机与设计

原 dispatcher 对状态下标整树做**二叉路由**（`buildDispatchTree`），
深度 log2(N)（luaV_execute 820 case ≈ 10 级），且遍历路径随 case 序列
随机变化，无法被分支预测器学习——预测失败是 dispatcher 的主要运行时
开销。改为两级分桶：

1. 桶选择：`bucketIdx = CaseValue & (桶数-1)` 纯掩码运算 + `switch`
   （后端跳转表，**单个间接跳转**，热点转移可被 BTB 学习）；桶数取
   ≥N/4 的最小 2 的幂，桶均 ~4 个成员。
2. 桶内解析：等值比较链（匹配则进 case，否则下一成员，链尾回
   default），期望 2~3 级。

混淆形态不弱化：switch 判据是加密表 volatile 取出的 CaseValue 掩码
（静态不可折叠），桶内仍是逐值比较，状态与块的映射依旧不可静态还原。
附带收益：路由结构从 ~2N 块（树内节点+叶）降为 N 块（比较链），二进制
体积近乎减半。注意 `applyFlatten` 末尾的 LowerSwitch 必须移除（用户的
switch 已在 planFlatten 入口统一降级），否则分桶 switch 会被展开回
比较树。

### 结果（vs base，6 轮中位，全部输出一致；v1=值掩码分桶）

| 应用 | 二叉树（旧） | 分桶 v1 | 分桶 v2（密度修复后） | 体积 v2 |
| --- | --- | --- | --- | --- |
| bzip2 | 19.52x | 13.27x | **6.66x** | 3.1x |
| zlib | 27.61x | 18.06x | **19.32x** | 4.7x |
| lua | 28.86x | 19.51x | **20.09x** | 5.0x |
| sqlite | 30.32x | 17.96x | **17.63x** | 5.5x |
| 几何平均 | 26.21x | 17.03x | **14.61x** | - |

（二叉树基线：lua 为同日修复轮数据，其余三应用为 full-v3 轮数据。）

### 追加修复：小 N 桶退化（v2，2026-10-16 下午）

v1 的桶数取 max(4, pow2(N/4))：N≤32 的函数桶数只有 4~8，实测 ARM64
后端对 ≤8 项的 switch **不生成跳转表**、退化为逐值比较链（bzip2 的
`BZ2_compressBlock` 约 30 case 正中此坑，压缩段比理论慢 ~4x；bcf 组合
反而"更快"的假象即源于此——BCF 切块把 case 数翻倍、意外推过跳转表
阈值）。v2 修复：桶数下限 16（N≥16 时），桶归属按 ordinal 轮转
（i % 桶数，N≥桶数时 100% 密度，必然生成跳转表），桶号以
`(ordinal + 逐构建随机旋转) & mask` 计算保持桶序与 ordinal 序脱钩。
bzip2 vmfla 13.27x → **6.66x**，四应用几何 17.03x → **14.61x**。

asm 验证：`bucketDispatch` 处生成 `ldrh w11,[x9,x8,lsl#1]; add x10,
x10,w11,lsl#2; br x10` 的 16 位偏移跳转表（luaV_execute 820 case / 249
桶生效）。回归 9 项全过（chain/indirectbr/lvars/complex/config/bcf/
phi/vmp）。与 amice 对照更新：vmfla 14.6x 落在其 basic 8.4x 与
dominator 19.0x 之间。

### 附带发现：构建非确定性

`CryptoUtils` 用 `Module::createRNG`，而 LLVM 的 RNG 默认**每进程随机
种子**（除非显式传 `-mllvm -rng-seed=`）——case 值/桶分配/块序逐构建
随机。同一配置两次编译产物不同（bzip2 vmfla 三次重编 compress 段
27.7~31s，±10% 构建间波动）。跨轮对比只看方向；同轮内比值才精确。
如需可复现构建，应固定 `-rng-seed`。

## 2026-10-16 组合复测：bcf + vmfla（分桶 v2，同轮四配置）

调度链上 bcf 在 bb2func 之后、merge/flatten 之前执行：切块与虚假块
进入 flatten 的 case 集。同轮测 base/bcf/vmfla/bcfvmfla，叠加比
（combo ÷ vmfla）为同轮比值：

| 应用 | bcf | vmfla | bcfvmfla | 叠加比 | 体积 combo |
| --- | --- | --- | --- | --- | --- |
| bzip2 | 4.15x | 6.66x | 18.27x | 2.74 | 4.5x |
| zlib | 8.84x | 19.32x | 17.92x | 0.93 | 6.4x |
| lua | 7.85x | 20.09x | **35.70x** | 1.78 | 7.2x |
| sqlite | 4.38x | 17.63x | 24.75x | 1.40 | 7.6x |
| 几何平均 | 5.96x | 14.61x | **23.19x** | **1.59** | - |

- lua 组合首次可跑（旧轮 run-timeout；luaV_execute 修复 + 分桶 v2
  后 35.70x、输出一致）——解释器 dispatch 循环同时吃 BCF 切块碎片
  与平坦化往返，叠加最重。
- 上轮 bzip2 出现的"combo 反而快于 vmfla"（叠加比 0.48）是 vmfla-only
  小 N dispatcher 退化假象（见上文 v2 修复），修复后恢复正常叠加。
- 叠加远低于相乘（5.96×14.61≈87x）：BCF 永假谓词的虚假边与真实边在
  flatten 后共享 dispatcher，只算一次路由成本，虚拟块链永不执行；
  逐应用叠加比在 0.9~2.7 间浮动，与热点函数的 case 规模/切块密度相关。

## 2026-10-16 常见算法开销：AES / MD5 / SHA-256 × vmfla、bcf+vmfla

函数级基准（`test/perf/workloads/algo_bench.c` + `test/perf/
algo_fn_bench.py`）：AES-128-CBC（T 表实现）、MD5、SHA-256 标准实现，
标注各算法顶层处理函数（整段 1MB 缓冲管线，压缩/轮函数内联其中），
-O2 直编，二进制内逐轮计时（150/60/60 轮取中位 × 3 次执行中位），
启动校验 FIPS-197 / RFC1321 / FIPS-180 官方向量，跨配置摘要必须一致：

| 算法 | vmfla | bcf | bcf+vmfla | bcf 叠加系数 |
| --- | --- | --- | --- | --- |
| AES-128-CBC | 4.60x | 4.89x | **4.54x** | ≈0（完全吸收） |
| MD5 | 7.10x | 2.87x | **7.68x** | +8% |
| SHA-256 | 10.79x | 6.43x | **11.15x** | +3% |

- **组合 ≈ 纯 vmfla**：BCF 的永假谓词边与真实边共享平坦化 dispatcher，
  虚拟块链永不执行，在这类"单函数大循环"形态上叠加成本接近零。
- 开销与**基本块转移密度**成正比：AES T 表轮函数近乎直线码（每 16B
  约 2~3 次转移）只有 4.6x；SHA-256 每块分支最多 10.8x。整程序基准
  的 vmfla 14.6x（几何）对应更碎的分支型业务代码。
- 三个函数均通过完整链路（bb2func 提取 5/16/16 + impl 拆分 + 分桶
  平坦化），日志与 IR 已核对。

### 追加复测：vmfla 链路精简后（同日）

vmfla 链路去掉 bb2func/merge 前段（保留方案，独立标注可用）、入口
伪调用点移除后，同一基准复测（分桶 v2、-O2、3 次执行中位）：

| 算法 | vmfla（旧链） | vmfla（纯平坦化） | bcf | bcf+vmfla（旧链） | bcf+vmfla（纯平坦化） |
| --- | --- | --- | --- | --- | --- |
| AES-128-CBC | 4.30x | **3.63x** | 5.20x | 4.84x | **23.75x** |
| MD5 | 7.35x | **6.72x** | 3.02x | 7.56x | **14.39x** |
| SHA-256 | 10.06x | **7.74x** | 6.81x | 11.31x | **27.40x** |

- 纯 vmfla 一致变轻（去 merge 分发层 + 无伪调用开销）。
- **组合从"被吸收"变为接近两者相乘**（AES 23.75 ≈ 5.20×3.63×1.26）。
  旧链的吸收是结构假象：bb2func 先把热点块提取成 helper（经 merge
  分发、不进状态机），BCF 切割只作用于剩余主体；新链 BCF 的切块
  碎片全部成为平坦化 case，每条碎片边都付一次 dispatcher 往返，
  开销 = 切块数 × 单次路由。AES 轮函数被切成最多碎片，叠加最重。
- 若要恢复"组合近零叠加"，可把 bcf 的切割阈值与平坦化联动（切割
  碎片直接融合为单 case），或按需恢复 bb2func 前段（保留方案）。

### 追加复测：算法扩展到 7 项（同日晚，-O0 口径定为标准）

fixture 扩展 DES（FIPS-46 教科书位置换实现）、SM4（GB/T 32907）、
RC4、CRC32（IEEE/zlib），全部过官方向量（DES 85e813540f0ab405 /
SM4 681edf34...4246 / RC4 bbf316e8...ad3 / CRC32 cbf43926）。
runner 解析改为按行名动态发现算法。**-O0 口径定为项目标准**
（混淆×优化已在编译期强制互斥，见「推荐用法」）：

| 算法 | vmfla | bcf | bcf+vmfla |
| --- | --- | --- | --- |
| AES-128-CBC | 1.71x | 2.47x | **12.11x** |
| DES | 2.48x | 1.28x | **3.15x** |
| SM4 | 1.40x | 1.10x | **3.38x** |
| RC4 | 2.50x | 2.57x | **14.84x** |
| MD5 | 5.18x | 2.15x | **11.97x** |
| SHA-256 | 1.82x | 1.56x | **7.24x** |
| CRC32 | 2.56x | 1.96x | **6.09x** |

分组看：块密码（AES/DES/SM4，直线轮函数）vmfla 1.4~2.5x、组合
3~12x；流/摘要（RC4/MD5/SHA/CRC，分支与循环转移密集）vmfla
1.8~5.2x、组合 6~15x。MD5 在 -O0 下基线本身几乎不吃亏（O0/O2 仅
1.5x），比值偏高；DES 位置换实现基线极慢（0.22s/MB），比值被稀释。

## 2026-10-16 测试流程定版 + CPython 应用基准

标准测试流程见 `docs/TEST_PROCESS.md`（约束 → 回归 → 算法 7 项 →
应用 → 手动重型）。新增 CPython 3.12.8 应用基准（`test/perf/
run_python_bench.py`）：configure + CC 记录器做一次基线构建并捕获
全部编译/链接命令，混淆配置按命令表重放两段式注入，静态库按 `ar t`
成员清单从镜像对象重建，混淆 exe 放回源码树内保证 stdlib 探测一致；
负载为确定性校验和脚本（数值/字符串/表/JSON）：

| 配置 | 中位 | vs base |
| --- | --- | --- |
| base | 0.185s | 1.00x |
| vmfla | 0.760s | **4.10x** |
| bcf+vmfla | 1.386s | **7.48x** |

全程序标注口径（约 330 个 C TU 全部混淆），输出跨配置一致。
Firefox 列入应用清单的重型手动层（mach/Rust 工具链，流程定义在
TEST_PROCESS.md，未在本机执行）。

### 追加复测：-O1 / -O0 口径（同日）

`algo_fn_bench.py` 增加 `--opt` 参数逐级编译（默认 O0）。
-O1 与 -O2 比值几乎一致（混淆后 IR 已是低可优化形态，后端优化
对分子分母影响趋同）；**-O0 比值显著更低**：分母换成未优化基线
（比 -O2 慢 1.5~5.6x），而混淆产物在 O0/O2 下速度接近（本身即
reg2mem+volatile 形态），得到的是变换的"结构内禀开销"：

| 算法 | vmfla O0 | bcf O0 | bcf+vmfla O0 | vmfla O1 | bcf+vmfla O1 |
| --- | --- | --- | --- | --- | --- |
| AES-128-CBC | **1.70x** | 2.48x | 12.87x | 3.61x | 22.96x |
| MD5 | 5.34x | 2.15x | 11.84x | 6.73x | 14.20x |
| SHA-256 | **1.82x** | 1.57x | 7.36x | 7.53x | 26.30x |

各基线绝对中位（O0→O2）：AES 4.5→2.0ms、MD5 4.2→2.8ms、
SHA-256 13.1→2.3ms；混淆产物 O0→O2 仅 ±30%。两端优化级别需一致
才有工程意义——对攻击者同样用 -O0 观察时，AES/SHA 平坦化的内禀
开销约 1.7~1.8x。

## 沿革（三轮数据对照）

| 轮次 | 变化 | 结果 |
| --- | --- | --- |
| 2026-10-09 初测 | 原 icall 架构；发现缺陷 1/3/4 + enstr zlib 失败（当时误判为 bl_order 误加密） | 建立基线；icall 跨 TU 修复 |
| 2026-10-10 合并复测 | 合并远程 8 提交（目录重构/VLLVMConfig/enstr mmap 池），icall 融合等级语义 + 专属跳板终版 | 无性能回归；修复远程 4 个 macOS 测试适配 + 位码过期重生成；zlib/enstr 仍失败 |
| 2026-10-10 终测 | enstr 跨 TU 根因定位与修复（本轮） | **本文数据**：全矩阵通过，zlib/enstr 转正 1.03x，其余 ±10% 内一致 |

## 复现方式

```bash
# 1. 源码：zlib/lua/sqlite/bzip2 tarball 解压到 test/perf/apps/（或 tmp/perfbench/apps/）
# 2. 全量矩阵（默认等级）
python3 test/perf/run_perfbench.py --apps bzip2,zlib,lua,sqlite \
  --configs base,fla,bcf,ibr,icall,vmfla,enstr,bb2func,merge,vmp,combo2 \
  --out tmp/perfbench/rerun --runs 6
# 3. 等级 2 专项
VLLVM_CONFIG=icall=2 python3 test/perf/run_perfbench.py \
  --apps bzip2,zlib,lua,sqlite --configs icall --out tmp/perfbench/l2
# 4. 失效定位（TU 级二分）
python3 test/perf/tu_bisect.py tmp/perfbench/rerun/lua/stage1 vllvm.vmfla \
  "{exe}" test/perf/workloads/bench.lua
# 5. VMP 函数级/算法矩阵
python3 test/perf/vmp_fn_bench.py
```

最小复现：icall 跨 TU `test/perf/repro/icall_x1.c`+`icall_x2.c`（回归
`./test/indirectcall/test_icall_cross_tu.sh`）；VMP CRC 断言
`test/perf/repro/vmp_crc32_trap.c`；enstr zlib 诊断驱动
`test/perf/repro/enstr_zlib_driver.c`。

## 局限与注意事项

- 测的是**全程序标注**的最坏情形；实际只标注少量敏感函数时，开销按所涉热点占比缩放。
- 两段式构建与源码直编有约 7% 基线偏差；横向比值不受影响。
- macOS arm64 单机测试；icall 的 x19/icallcc 约定、vmp 准入条件在其他目标可能不同。
- `ld64.lld` 与 macOS 27 SDK tbd 不兼容是环境问题（见「测试方法」第 4 条）。
- 遗留缺陷（ibr+vmfla 组合、VMP 问题清单）修复后重跑上述命令即可复测对比；lua/vmfla 已于 2026-10-16 修复。

## 2026-10-20 复测：BCF 谓词常量比较 + 模块级共享导出 export_write（weak）

工具链变化（其余方法与本报告一致，本轮全部 `-O0`）：

- 谓词从"两次 volatile 读自比较（`x == x op K`）"改为 **`(seed op K) ==/!= 随机常量 C`**：C 避开默认值 0 过同一运算的期望结果，极性按改写点随机（`==` 恒假 / `!=` 恒真，真假边排布对调）——反编译视图无自比较签名；恒定性以 seed 保持默认 0 为前提（uint16 双射，对常量比较不存在取值无关构造），外部库约定只读。
- seed 从每函数 `vllvm.bcf.seed.<函数名>` 收敛为模块级共享导出变量 **`export_write`**（WeakAnyLinkage，不带 vllvm 前缀）；函数内不再初始化（含入口归一链删除）。
- 本轮发现并修复缺陷 #6（多 TU 强定义重定义冲突 → weak 合并），修复后 16/16 配置全过、输出与基线逐字节一致。
- 原始数据：L2 `tmp/perfbench/algo-vmfla/results_O0.json`；L3 `tmp/perfbench/bcf-export-write-weak/results.json`。

### L2 算法基准（7 算法，vs base）

| 配置 | aes | crc32 | des | md5 | rc4 | sha256 | sm4 | 范围 |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| bcf | 2.53x | 2.14x | 1.28x | 2.18x | 2.63x | 1.57x | 1.07x | **1.07~2.63x** |
| vmfla | 1.73x | 2.58x | 2.58x | 5.13x | 2.46x | 1.84x | 1.39x | 1.39~5.13x |
| bcfvmfla | 12.49x | 5.91x | 2.78x | 11.99x | 14.51x | 7.17x | 3.23x | 2.78~14.51x |

### L3 应用基准（4 应用 × 4 配置，vs base 中位数）

| 配置 | bzip2 | zlib | lua | sqlite |
| --- | --- | --- | --- | --- |
| bcf | 1.69x | 2.42x | 2.39x | 1.74x |
| vmfla | 4.32x | 4.06x | 4.20x | 6.72x |
| bcfvmfla | 10.08x | 11.74x | 8.64x | 11.02x |

### 要点

- **多 TU 修复**：首轮 L3 的 bcf/bcfvmfla 共 8 配置全部 `duplicate symbol _export_write` 链接失败（强定义）；改 WeakAnyLinkage 后全过。注意 bzip2/zlib 首轮同样失败——此前"每函数唯一符号名"掩盖了多 TU 场景。
- **bcf 开销较 2026-10-13 节明显回落**（L3 运行时 8.65x → 1.7~2.4x）：该节之后切割粒度已放宽（≥8 条才切、每段至少 4 条，单后继分支改写点大幅减少），本轮再叠加谓词链精简——单次 volatile 读 + 单运算 + 常量比较，函数内无 seed 初始化链；L2 趋势一致（1.07~2.63x）。
- 正确性门槛全过：L2 官方测试向量 + 四配置摘要逐一相等；L3 每轮 stdout 与 base 逐字节一致；新增 L1 回归 `test/boguscontrolflow/test_bcf_multi_tu.sh`。

### 其余 Pass 全量复测（ibr / icall / enstr / bb2func / merge / vmp）

同轮工具链（`-O0`，含缺陷 #6/#7 修复后编译器），全程序标注、6 轮取中位；
28/28 配置全过、输出与基线逐字节一致。原始数据
`tmp/perfbench/allpasses-1020-fixed/results.json`（首轮
`allpasses-1020` 中 bzip2/vmp 因缺陷 #7 链接失败，仅作过程记录）。

运行时（vs base）：

| Pass | bzip2 | zlib | lua | sqlite |
| --- | --- | --- | --- | --- |
| ibr | 2.16x | 2.01x | 1.73x | 2.35x |
| icall | 1.07x | 1.04x | 1.18x | 2.44x |
| enstr | 0.99x | 1.01x | 1.03x | 1.00x |
| bb2func | 1.74x | 1.98x | 1.22x | 1.88x |
| merge | 1.04x | 1.06x | 1.08x | 1.43x |
| vmp | 1.02x | 3.47x | 1.54x | 15.18x |

二进制体积（vs base）：ibr **2.1~2.6x**；icall **1.1~1.5x**；enstr
**1.1~1.2x**；bb2func **1.6~2.0x**；merge **1.1~1.3x**；vmp
**1.1~1.4x**（bzip2 1.22x / zlib 1.24x / lua 1.43x / sqlite 1.08x）。

要点：

- **缺陷 #7 修复后 vmp 四应用全过**：bzip2/vmp 1.02x（全程序标注下热点
  回退为主，与既有结论一致）、sqlite/vmp 15.2x（174 个中层函数被虚拟化
  的既有形态），vmp 编译耗时 sqlite 213s（≈460x base 的 0.5s）仍是
  编译开销主项。
- **与「结论速览」（2026-10-10）相比整体比值更低**：该轮为 -O2 时代
  口径，混淆×优化互斥（`1e355f2`）后一切混淆构建 `-O0`——基线本身
  变慢、同混淆代码的相对比值相应压缩。同口径纵向对比以本节为准。
- enstr 恒定 ≈1.0x；icall 仅 sqlite 明显（2.44x，跨 TU 仅入池间接化、
  热点集中于少数内部函数）；merge/bb2func/ibr 与 10-10 结论方向一致、
  数值随 -O0 口径回落。

## 2026-10-20 组合测试（策略定版 + 缺陷 #3 修复）

**组合策略定版**：三组组合，不与 bcf 叠加；**不向全部函数施加混淆**——
应用级组合经 `--annotate-fraction` 选择性注入（每 TU 按函数规模降序取
前 50%），算法级本就只标注各算法顶层处理函数。

| 配置 | 标注集 |
| --- | --- |
| combo1 | enstr + icall + ibr |
| combo2 | vmfla + ibr |
| combo3 | enstr + icall + vmfla |

组合测试三层口径：组合性能只在算法级测，应用级组合只做可用性
（编译成功 + 单次执行输出与基线一致，`--runs 0`）。

### 缺陷 #3 修复：vmfla+ibr 调度顺序（combo2 的前置修复）

旧调度反序执行（ibr 在函数分发早期先改写，vmfla 晚期再面对已被改成
indirectbr 的 CFG）：地址表跳转网络被当孤岛排除在状态机外、入口边
错接 dispatcher，函数体整体不执行——最小复现 `mix(7)` 原样返回 7
（base 244）。修复（`PassInfo.cpp` `IbrLateAttr`）：同函数两者都标注
时 ibr 延迟到晚期分发**紧随 vmfla** 执行，作用于平坦化产物
`<fn>.vllvm.impl`；vmfla 回退时作用于原函数。修复后 28 对冒烟全过、
`test_vmfla_indirectbr`（computed goto 输入场景）无回归、L1 27/27。

### 逐对组合冒烟（L1 新增）

`test/complex/test_pair_combos.sh`：8 个 pass（fla 为 vmfla 别名不重复
入池）两两组合 × 28 对，编译 + 运行 + 退出码等价，**28/28 全过**。

### 算法级组合性能（vs base，L2，只标注顶层处理函数）

| 配置 | aes | crc32 | des | md5 | rc4 | sha256 | sm4 | 范围 |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| combo1（enstr+icall+ibr） | 1.74x | 1.24x | 2.00x | 2.92x | 1.21x | 1.21x | 1.43x | **1.2~2.9x** |
| combo2（vmfla+ibr） | 3.11x | 6.97x | 4.03x | 20.08x | 5.34x | 4.15x | 2.59x | **2.6~20.1x** |
| combo3（enstr+icall+vmfla） | 2.18x | 2.45x | 2.49x | 4.99x | 2.20x | 1.78x | 1.73x | **1.7~5.0x** |

要点：combo1 最轻（enstr≈1.0x、icall 热路径近零，开销基本来自 ibr）；
combo2 最重（vmfla × ibr 相互放大，md5 达 20x）；combo3 居中。历史
参考：同日旧口径组合（含 bcf、全函数标注）曾测得 10.5~43.9x——
bcf 参与叠加与全函数标注是当时开销居高的两个因素。

### 应用级组合可用性（L3，--runs 0，--annotate-fraction 0.5）

| 配置 | bzip2 | zlib | lua | sqlite |
| --- | --- | --- | --- | --- |
| combo1（enstr+icall+ibr） | ✅ | ✅ | ✅ | ❌ 缺陷 #8（SIGBUS，输出完整后崩溃） |
| combo2（vmfla+ibr） | ✅ | ✅ | ✅ | ✅ |
| combo3（enstr+icall+vmfla） | ✅ | ✅ | ✅ | ✅ |

原始数据 `tmp/perfbench/combo-1020-final/results.json`（combo1 sqlite
失败已定界至单函数：函数级二分收敛到 shell.c `process_input`，仅对该
函数施加 enstr+icall 即复现；`--annotate-fraction 0.5` 下每 TU 标注
规模降序前 50%）。注意逐对冒烟（28/28，小 fixture）未触发 #8——该
缺陷是**寄存器压力敏感**的，小函数不会把活跃值分配到 X19 跨 icallcc
调用。

### 编译耗时结论（回应"编译卡在哪"）

组合编译**不是瓶颈**：bzip2 全部 8 TU 并行 + 链接共 0.2~0.3s（手动逐
TU 实测最慢 1s）；此前误读的"20 分钟"实为 1 次预热 + 6 次计时的
**执行**时间（combo2 单次执行 ≈ 195~302s，与算法级开销一致）。真正
编译耗时的是 **vmp×sqlite（213s，VMP 字节码编译）**，与本组合策略
无关（组合不含 vmp）。组合测试轮的墙钟几乎全部由混淆产物的单次
执行时间构成——这也是组合性能只在算法级测、应用级只做单次可用性
验证的原因。

### enstr 新运行时应用级验证（协议改版后）

enstrpool.c 协议改版（i32 下标、key[8] 循环异或、无 CAS 单线程首次
初始化）配套 EncryptoStrPass 后，应用级可用性 4/4 全过（编译成功 +
单次执行输出与基线逐字节一致，`--runs 0`）：

| 配置 | bzip2 | zlib | lua | sqlite |
| --- | --- | --- | --- | --- |
| enstr（全函数标注） | ✅ | ✅ | ✅ | ✅ |
| combo3（enstr+icall+vmfla，0.5 选择性标注） | ✅ | ✅ | ✅ | ✅ |

过程中修复 L3 之外的 LTO 误编译：vmp+enstr+LTO 下解密循环的
`key[j & 7]` 被向量化成跨描述符越界读（长度 ≥32 的字符串前 8 字节
外全部错误），密钥读取加 volatile 阻断（见 `enstrpool.c` 注释）。
原始数据：`tmp/perfbench/enstr-new-1020{,-combo}/results.json`。
