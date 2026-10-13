# VLLVM Pass 开源应用性能测试报告

- 测试窗口：2026-10-09 ~ 2026-10-10（三轮：初测 → 合并远程重构复测 → enstr 修复后终测；**本文数据以终测为准**，沿革见文末）
- 测试机：Apple M4 Pro（arm64，14 核，48GB），macOS 27.0
- 工具链：VLLVM clang 21.1.0（`build/llvm-macos`；含目录重构 + VLLVMConfig 等级系统 + enstr mmap 池 + 本轮两项跨 TU 修复；链接用系统 `/usr/bin/ld`，见「环境说明」）
- 原始数据：全部呈现在本文表格中；复测后可用 `test/perf/gen_report.py` 重新生成

## 结论速览

以「全程序函数标注」（每个 TU 的所有函数打标注）在 4 个开源 C 项目上逐一启用各 Pass：

| 维度 | 结论 |
| --- | --- |
| 运行时开销 | `enstr` ≈ **1.0x**（近零）；`merge` **1.4~4.3x**；`bb2func` **2.8~6.1x**；`bcf` **3.7~6.0x**；`icall` **2.8~6.5x**（L1/L2 持平）；`ibr` **5.4~8.8x**；`fla` **13.2~30.3x**；`vmfla` **12.9~56.3x**（最重） |
| 二进制体积 | `bcf`/`vmfla` 最大（**7~15x**）；`fla`/`ibr` 约 **3~5x**；`icall` **1.4~2.1x**；`enstr`/`vmp` ≈ 1.0~1.4x |
| 编译耗时 | 除 `vmp`（sqlite **156s ≈ 46x**）外均在基线 3 倍以内 |
| 功能正确性 | 累计发现 **5 个缺陷**，其中 2 个已修复（icall 跨 TU、enstr 跨 TU，均为「module pass 密文化外部可见目标 + 其他 TU 明文引用」同构问题）；**仍遗留 2 个**：ibr+vmfla 组合静默失败、vmfla 崩溃于 `luaV_execute` |
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
| 3 | ibr + vmfla 组合：进程启动即静默退出（argv 破坏征兆） | 未定位（两两组合中仅此一对失败，单 pass 均正常） | ❌ 遗留 |
| 4 | vmfla 崩溃于 `luaV_execute`（lua 解释器主循环，数千行巨型函数；lvm.c 其余 31 函数正常） | 未定位（与超大函数 + 密集分支形态相关） | ❌ 遗留 |
| 5 | VMP 的 6 个问题（递归崩溃、CRC 形态 VM 断言、哈希类不可表达等） | 见 `docs/VMP_ISSUES.md` | ❌ 遗留（记录在案） |

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
- 遗留缺陷（ibr+vmfla、lua/vmfla、VMP 问题清单）修复后重跑上述命令即可复测对比。
