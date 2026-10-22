# VLLVM for LLVM 21.1

VLLVM 将 OLLVM 风格和实验性 VMP 混淆 Pass 集成进 LLVM/Clang 源码树，构建产物是原生 `clang`，不依赖 `-fpass-plugin`。混淆通过 `annotate("vllvm:...")` 标记精确作用到函数或字符串相关对象。

## 已实现混淆

| 标记 | Pass | 说明 |
| --- | --- | --- |
| `enstr` | EncryptoStrPass | 字符串加密，Module Pass；全部密文平铺进一张匿名 mmap 池，调用点经 `__vllvm_enstr_get(i32 下标)` 取址，首次访问按描述符（偏移/长度/`key[8]`）循环异或解密，无明文全局、无跨模块符号。 |
| `vmfla`（表参数化） | moveTablesToImplParams | 函数级混淆完成后把 vmfla 等每函数常量表改为参数传递：函数体搬进带表参数的私有 `vllvm.impl`，原函数退化为传表包装器，并以 volatile 守卫的伪调用点阻断 -O2 过程间常量传播把参数折叠回全局。indirectbr/musttail 等场景整体回退。 |
| `icall` | IndirectCallPass | 标注在被调函数上：模块内所有调用点改写为对**各自专属 naked 跳板函数**（`__vllvm_icall.<id>.call.<n>.<fn>`，签名 = 目标签名 + 隐藏 `i32 nest` 调用号）的直调；调用方先把注册号（`group_id<<16\|index`）作为调用点常量经 icallcc 的 nest 通道写入 w16（`mov w16, #imm`，IP0 天然 caller-saved），跳板函数体是按目标随机生成的汇编——x0~x7 上就地应用参数的随机可逆运算链加密（仅 add/sub/xor），再用 `ubfx/and` 从 w16 解码 (group,index) 查随机注册的调用池，经 `ldr x17`/`ret x17` 跳转到真实目标（与 `src/funccaller` 的 call_func 序列一致）；池在 `init_array` 乱序注册。icallcc 约定参数/返回沿用标准 ABI 位置、CSR 即平台标准集（nest 不占任何 callee-saved 寄存器）。等级语义（`-mllvm -vllvm-config=icall=<n>`）：0 关闭；1（默认）仅专属跳板间接化；2 再叠加参数加密与调用点 fake 诱饵下标（过同款可逆链的假 nest 常量写入诱饵全局，淹没真实调用号）。目标入口把参数解密一次写回参数栈槽（`参数 = 解密的参数`），之后的使用全部按左值（volatile 槽位读取）进行，优化器无法把解密算术重物化回使用点。地址被取用、存在不可改写调用点（invoke/musttail/vmp 调用方等）、或非内部链接的目标不参与参数加密，仅入池。加密覆盖 x0~x7 寄存器传入的 32/64 位整数与普通地址空间指针（入口经 ptrtoint/inttoptr 还原；arm64e/PAC 目标与非零地址空间指针保持明文）；浮点（v 寄存器）、栈传参数与 8/16/128 位整数保持明文。 |
| `ibr` | IndirectBranchPass | 先将 `switch` 降为 if/else 分支，再随机使用混合、ADD、XOR、SUB 或明文模式处理下标，动态查全部非入口块的地址表（LLVM 禁止对入口块取地址）。 |
| `bcf` | BogusControlFlowPass | 独立虚假控制流，先做前置处理：真实块大块切割（≥8 条有效指令切成 4~6 条的碎片（每段至少 4 条），碎片间无条件 br 相连，切出的边本身成为后续改写点）、生成随机指令虚假块、所有块重排（入口固定首位）；随后 seed 为模块级**导出**全局变量（WeakAnyLinkage + 默认可见性，符号名 `export_write`——刻意不带 vllvm 前缀，避免符号自曝混淆器特征，**所有 bcf 函数共享**：首个 bcf 函数创建、后续复用；外部库可按该符号引用；初始化器为 0，不携带明文；真实执行流只读，多函数/多线程共享不互相干扰；多 TU 各自使用 bcf 时各模块以 weak 定义导出，链接器合并为全程序同一变量），**函数内不再初始化**，默认运行时值即 0——改写点谓词以该默认值为镜像基点，外部使用约定为**引用读取、运行期不改写**（见下）。运算统一 uint16（i16 回绕），各改写点 volatile 读全局一次、过一次**非零** add/sub/xor 运算（无乘法、不叠加多轮）后与随机常量比较——谓词形如 `(seed op K) ==/!= C`，常量 C 避开默认值 0 过同一运算的期望结果，极性按改写点随机二选一（`==` 恒假：假值边回原后继；`!=` 恒真：真值边回原后继），真假后继排布随之对调，指向虚假后继的边永不可达；volatile 读不可折叠，反编译视图呈现为对外部可配置全局的常规常量比较，无 `x == x + c` 自比较签名。**注意**：uint16 上 add/sub/xor 均为双射，对常量比较不存在取值无关的永假构造，恒定性以 seed 保持默认 0 为前提——外部库运行期写入毒值（C 反运算 K）会翻转虚假边；对所有单后继分支创建虚假条件分支——真边回原后继，假边随机落入函数内任意块（真实块与虚假块均可作虚假后继）；虚假块指令以真实块指令为参考克隆（保留原值名/调试位置/元数据，反编译视图中与真实代码同形；跨块操作数替换为常量或 seed 槽），互相链接成链、链尾接回真实块，并强制锚定在至少一条永假分支后继上（未引用的不可达块不会进后端发射，反编译时会消失）。等级（`-mllvm -vllvm-config=bcf=<n>`）：1/2/3 单后继分支覆盖率 60%/85%/全量，虚假块数量递增。 |
| `flower` | FlowerPass | 花指令：模板库（`src/c_func/flower/`，仅 AArch64，其他架构目标不插入）经 inline asm 在函数内随机位置插入**汇编级**花指令，目标是干扰反汇编/反编译：跳过型 `b 1f`/`cbz xzr, 1f`（xzr 恒 0，恒真不透明谓词）跳过 0~2 个随机垃圾 `.word`——垃圾字节运行时永不执行，但占据 `.text`，线性反汇编器照常解码而出错失步（随机编码大概率解码为无效/无意义指令）；算术型 `add/sub/eor/orr/and xzr, xA, xB`（写入零寄存器被硬件丢弃，源操作数只读任意通用寄存器，无副作用）真实执行且在反汇编视图中与真实运算代码同形，无法按可达性剔除。实现为无约束、带副作用的 inline asm 调用：无操作数即无寄存器分配交互，任何优化级别不可消除/折叠；asm 首行注释标记 `vllvm.flower` 只存在于 IR（计数用），注释不进目标文件、无符号/特征泄漏；注释符随 triple（darwin `;` / ELF `//`）。插入点为全函数随机锚点（跳过 PHI、EH pad、musttail 块、调试伪指令与入口前导 alloca 段），不改 CFG、不引用已有值。等级（`-mllvm -vllvm-config=flower=<n>`）：**level x = 每个标注函数插入 x 条花指令**（默认 1，上限 32，0=关闭；函数过小无可用锚点时自然少于 x）。先于 bcf/ibr 执行，保证计数不落入永不可达虚假块。 |
| `vmfla` | VMFlattenFuncPass | VM 风格函数平坦化，共享一张整数常量表，见下文。 |
| `vmp` | VmpPass | 实验性 Virtual Machine Protection。 |

支持集即上表七个标注（bcf/flower/vmfla/icall/enstr/ibr/vmp），不再提供任何
别名或旧名兼容；未知名标注会打印
`[vllvm] unknown annotation kind '<名>' ignored` 告警并跳过。

## vmfla

`vllvm:vmfla` 执行**平坦化本体**：

```text
vmfla（入口直连 wrapper -> 带 table 参数的私有 impl -> 分桶 dispatcher 状态机）
```

早期链路的 bb2func/merge 前置段已随别名清理一并移除（2026-10-20）。

等级语义（`-mllvm -vllvm-config=vmfla=<n>`，默认 1）：

- 0：vmfla 全局关闭。
- 1：平坦化混淆。
- 2：再开启局部变量结构化（lvars）。

## 构建依赖

三端统一需要 `clang`、`clang++`、`cmake`、`ninja` 和 `git`。构建脚本会应用 `patches/` 中的 LLVM 集成补丁，并同步 `src/` 到 LLVM 源码树内的 `llvm/lib/Transforms/VLLVM`。

macOS 如果工具来自 Homebrew，可先设置：

```bash
export PATH="/opt/homebrew/bin:$PATH"
```

## 构建

Linux：

```bash
NO_CLONE=1 JOBS=8 ./build-linux.sh
./build/llvm-linux/bin/clang input.c -o input
```

macOS：

```bash
NO_CLONE=1 JOBS=8 ./build-macos.sh
./build/llvm-macos/bin/clang input.c -o input
```

Windows：

```powershell
.\build-windows.ps1
.\build\llvm-windows\bin\clang.exe input.c -o input.exe
```

使用 `clang-cl` 构建 Windows 版本：

```powershell
.\build-windows.ps1 -CCompiler clang-cl -CXXCompiler clang-cl
```

如果本地没有 `llvm-project-21.1.0/`，不要设置 `NO_CLONE=1`，脚本会尝试自动 clone 对应 LLVM tag。

## 推荐用法：-O0 编译 + 混淆

项目的确定用法是**不开编译优化**（`-O0`）编译混淆目标，且已做成编译器
硬性约束：模块内存在任何 `vllvm:` 标注而优化级别高于 -O0 时，编译直接
报错并拒绝产出二进制（管线起点检查；无标注的普通 -O1/-O2 编译不受
影响）。所有 VLLVM pass 以 `isRequired()` 挂在管线里，`-O0` 下照常执行，
无需额外开关；测试套件全部按 `-O0` 口径运行。

源码标注（精确到函数，逗号分隔可叠加）：

```c
__attribute__((annotate("vllvm:vmfla,bcf")))
static unsigned crypto_core(unsigned x) { /* ... */ }
```

编译与链接：

```bash
# 编译：-O0 -g0（-g0 去掉调试信息，混淆产物不携带源码行号）
./build/llvm-macos/bin/clang -O0 -g0 -c secret.c -o secret.o

# 等级配置（全局调强度；作用目标仍由 vllvm: 标注决定，0=关闭）：
./build/llvm-macos/bin/clang -O0 -g0 -mllvm -vllvm-config=vmfla=2 \
    -c secret.c -o secret.o

# 链接后 strip 掉符号（-O0 会保留 static 函数名于符号表）
./build/llvm-macos/bin/clang -O0 -g0 *.o -o app && strip app
```

为什么禁止 `-O1/-O2`：

- 过程间常量传播（IPSCCP/GlobalOpt）会把混淆形态改回去——例如 vmfla
  的常量表参数被折叠回固定全局引用，反编译视图重新拿到表地址。与其
  产出形态失真的混淆二进制，不如在编译期拒绝。
- 实测混淆产物在 `-O2` 下几乎没有额外提速（±30%，产物本身已是
  reg2mem + volatile 的低可优化形态），优化收益极小而形态变差。
- 同级（-O0）基线的函数级开销参考：AES-128-CBC / MD5 / SHA-256 标注
  顶层函数时 vmfla 约 1.7x / 5.3x / 1.8x，bcf+vmfla 约 12.9x /
  11.8x / 7.4x，数据与口径见 `docs/PASS_PERF_REPORT.md`。

## 测试

标准测试流程见 `docs/TEST_PROCESS.md`（分层：约束检查 → 回归 →
算法基准 → 应用基准 → 手动重型），提交前各层必须全绿。

算法基准覆盖 AES/DES/SM4/RC4/MD5/SHA-256/CRC32：

```bash
python3 test/perf/algo_fn_bench.py --runs 3
```

应用基准覆盖 zlib/bzip2/lua/sqlite 与 CPython：

```bash
python3 test/perf/run_perfbench.py --apps zlib,bzip2,lua,sqlite \
    --configs base,vmfla,bcfvmfla --runs 6
python3 test/perf/run_python_bench.py --runs 6
```

Firefox 为手动重型层（mach/Rust 工具链，流程见 `docs/TEST_PROCESS.md`）。

构建完成后运行对应 shell 回归测试。常用入口：

```bash
./test/enstr/test_enstr.sh
./test/enstr/test_enstr_phi.sh
./test/enstr/test_enstr_cache.sh
./test/phi/test_phi_lowering.sh
./test/indirectcall/test_indirectcall.sh
./test/indirectcall/test_icall_verifier.sh
./test/indirectcall/test_icall_cross_tu.sh
./test/indirectbranch/test_indirectbr.sh
./test/localvarstruct/test_lvars.sh
./test/localvarstruct/test_lvars_fla.sh
./test/boguscontrolflow/test_bcf.sh
./test/flower/test_flower.sh
./test/complex/test_passes_complex.sh
./test/config/test_config.sh
```

VMP 相关测试：

```bash
./test/vmp/test_interpreter.sh
./test/vmp/test_target.sh
./test/vmp/test_vmp.sh
./test/vmp/test_runtime_sdk.sh
./test/vmp/test_fallback.sh
./test/vmp/test_cross_targets.sh
```

vmfla 链路测试：

```bash
./test/vmfla/test_vmfla_chain.sh
```

测试脚本会优先使用 `build/llvm-macos/bin/clang` 或 `build/llvm-linux/bin/clang`，也可通过 `CLANG=/path/to/clang` 指定编译器。
新增的 IR 独立校验脚本还需要同一构建目录中的 `llvm-as`（可通过 `LLVM_AS` 覆盖）。

开源应用级 Pass 性能基准见 `test/perf/`（zlib/lua/sqlite/bzip2 全程序标注，
测运行时/体积/编译开销与正确性），结果报告在 `docs/PASS_PERF_REPORT.md`。
Endless Tunnel 的 Android APK 构建、IR 校验和真机核心算法测试见 `test/android/README.md`。

`enstr` 的池运行时（`src/c_func/enstrpool/enstrpool.c`，编成位码随目标
模块链入）在首次访问时一次性 mmap 匿名内存、把密文 blob 整体拷入，随后按
描述符表（`{u32 offset, u32 size, u8 key[8], u8 isdec}`）逐串用
`key[j & 7]` 循环异或解密——描述符内解密标记保证单线程首次初始化语义，
无需 CAS/原子操作。调用点统一经 `__vllvm_enstr_get(i32 下标)` 取
`池基址 + 偏移`，IR 中不再出现明文绝对地址；密钥读取为 volatile，链接期
优化（LTO 向量化）不会把解密折叠回常量。池不释放，明文驻留至进程退出
（静态字符串生命周期）；这不是"使用后擦除"模式，调用方不应 `free` 返回
地址。

## 公共 PHI 降级

`Utils.h` 的 `lowerPHINodes(Function &F)` 由 `enstr`、`vmfla` 等
pass 共用：PHI 转换为入口块的 `alloca`、前驱块的
`store` 和汇合块的 `load`，底层使用 LLVM `DemotePHIToStack`，保留循环
PHI 的并行赋值语义。`invoke` 的 normal 边先拆出跳板，确保返回值在调用
完成后才存入栈槽；生成的访存指令保留调试位置，字符串解密调用也保留
EH funclet 上下文。

工具返回 `Unchanged`、`Lowered` 或 `Unsupported`。遇到不可存储类型、
PHI 所在块/前驱是 `catchswitch`，或 incoming 是该前驱 `callbr` 的返回值时，
整个函数保持不变。`vmfla` 在改写 CFG 前回退；`enstr` 跳过仍被 PHI
引用的字符串。字符串加密先降级再收集 users，不再单独处理 PHI；
`fixStack`/`fixStackForFlatten` 复用该工具，额外处理普通跨块 SSA 值。

降级约束只针对这些 Pass 的处理阶段；后续 LCSSA、mem2reg 等 LLVM 优化
可能重新生成 PHI。`test/phi/test_phi_lowering.sh` 同时检查 Pass 后的 IR、
`-O0/-O2` 运行结果、异常传播和不支持场景的完整回退。

## 项目结构

```text
src/Pass/                Pass 实现（bcf/flower/vmfla/icall/enstr/ibr/vmp）
src/attribute/           注解解析（VLLVMAttribute）
src/common/              公共工具（Utils、CryptoUtils）
src/config/              全局混淆等级配置（VLLVMConfig）
src/include/             Pass 和公共头文件
src/c_func/vminterpreter/ VMP 解释器与 runtime bitcode 生成
src/c_func/enstrpool/    enstr 字符串池 C 运行时（mmap 匿名内存 + 下标访问）
src/c_func/funccaller/   间接调用运行时辅助
src/c_func/flower/       花指令模板库（IR 生成器，编进 pass 而非链入目标）
src/VMP/                 实验性 LLVM VMP target 与 runtime 嵌入
patches/                 LLVM/Clang 集成补丁
test/                    按 pass 分组的回归测试
docs/                    设计文档（含 PROGRESS.md 进度）
```

开发时优先修改 `src/` 和 `patches/`。`llvm-project-21.1.0/` 是构建工作树，VLLVM 文件由脚本同步生成，不应作为主源码维护。

## 可选安装

脚本不会主动安装。确认 build 目录中的工具可用后，可手动安装：

```bash
cmake --install build/llvm-linux --prefix /opt/vllvm
cmake --install build/llvm-macos --prefix /opt/vllvm
```

Windows：

```powershell
cmake --install .\build\llvm-windows --prefix C:\vllvm
```

## 参考资料

- [amice](https://github.com/fuqiuluo/amice)
