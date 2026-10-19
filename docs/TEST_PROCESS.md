# VLLVM 混淆项目测试流程

本文规定 VLLVM 的标准测试流程。所有混淆与编译优化互斥（编译期强制，
见「第 0 层」），一切混淆构建一律 `-O0 -g0`。

## 流程总览（提交前必须全绿）

| 层 | 内容 | 入口命令 | 时长 |
| --- | --- | --- | --- |
| L0 约束检查 | 混淆×优化互斥、-O0 正常编译 | `./test/config/test_obf_requires_o0.sh` | 秒级 |
| L1 回归测试 | 每个 pass 的 IR 形态断言 + 运行等价 | `test/` 下全部 `.sh` | ~10 分钟 |
| L2 算法基准 | 7 算法 × 4 配置，向量校验 + 开销 | `python3 test/perf/algo_fn_bench.py --runs 3` | ~10 分钟 |
| L3 应用基准 | 4 应用全程序标注（zlib/bzip2/lua/sqlite） | `python3 test/perf/run_perfbench.py --apps zlib,bzip2,lua,sqlite --configs base,vmfla,bcfvmfla --runs 6` | ~40 分钟 |
| L3+ 应用基准 | CPython（重型） | `python3 test/perf/run_python_bench.py --runs 6` | ~1 小时（首次含源码下载与基线构建） |
| L4 手动重型 | Firefox（见下） | 人工触发 | 数小时 |

改动 pass 时：L1 对应目录脚本必须同步更新；行为变化需追加 L2/L3 复测
数据到 `docs/PASS_PERF_REPORT.md`。

## 第 0 层：约束检查

- 任何 `vllvm:` 标注 + `-O1/-O2/-O3/-Os` → 编译失败且无产物
  （管线起点 `VLLVMAnnotationPass` 强制）。
- `-O0` + 标注正常；无标注的任意优化级别不受影响。
- 入口：`./test/config/test_obf_requires_o0.sh`。

## 第 1 层：回归测试

`test/` 下按 pass 分组的 shell 脚本，全部 -O0 口径：

```bash
# 全量
for t in $(find test -name '*.sh' -not -path '*/perf/*' \
      -not -path '*/android/*' -not -path '*/out/*'); do bash $t; done
```

核心入口：`test/complex/test_passes_complex.sh`（组合）、
`test/config/`（等级语义与约束）、`test/vmfla/`（链路与 indirectbr）、
`test/boguscontrolflow/`、`test/enstr/`、`test/indirectcall/`、
`test/indirectbranch/`、`test/phi/`、`test/localvarstruct/`、
`test/bb2func/`、`test/merge/`、`test/vmp/`。

## 第 2 层：算法基准（函数级）

覆盖 **AES-128-CBC、DES、SM4、RC4、MD5、SHA-256、CRC32**：

- fixture：`test/perf/workloads/algo_bench.c`——标准实现 + 官方测试
  向量（FIPS-197 / FIPS-46 / GB/T 32907 / RFC6229-ish / RFC1321 /
  FIPS-180 / CRC32 "123456789"），标注各算法顶层处理函数，
  `OBF_MODE` 选择配置（0 base / 1 vmfla / 2 bcf / 3 bcf+vmfla）。
- 运行：`python3 test/perf/algo_fn_bench.py --runs 3 [--opt O0]`，
  样本链接后统一 strip 再计时（-O0 会保留 static 函数名于符号表）。
- 正确性门槛：向量全过、轮间摘要无漂移、四配置摘要逐一相等，
  任一失败脚本非零退出。
- 产出：`tmp/perfbench/algo-vmfla/results_O0.json`（中位数与比值）。

## 第 3 层：应用基准（全程序标注）

**zlib / bzip2 / lua / sqlite**（自动）：

```bash
python3 test/perf/run_perfbench.py \
  --apps zlib,bzip2,lua,sqlite \
  --configs base,bcf,vmfla,bcfvmfla --runs 6 \
  --out tmp/perfbench/<轮次名>
```

- 两段式：stage1 生成 -O0 IR → `inject_vllvm_attrs.py` 注入全函数标注
  → stage2 `-O0` 编译（混淆不允许优化）。
- 正确性门槛：每轮 stdout 与 base 参照全等，输出不一致记
  `output-mismatch` 并视为失败。
- 应用源缓存在 `test/perf/apps/`（或 `tmp/perfbench/apps/`）。

**python**（自动，重型）：`python3 test/perf/run_python_bench.py`

- CPython 3.12.8：configure + CC 记录器做一次基线构建并捕获命令表；
  混淆配置按命令表重放（.c 两段式注入、ar/link 换镜像 .o）。
- 负载：`test/perf/workloads/bench_python.py`（数值/字符串/表/JSON，
  确定性校验和）；跨配置输出必须一致。

**Firefox**（手动，最重型）：

- 为什么手动：需要 mach bootstrap（Rust/cbindgen/Node 等工具链）、
  源码 >10GB、全量构建数小时，不适合常规回归。
- 流程：
  1. `git clone --depth 1 https://github.com/mozilla/gecko-dev`（或
     官方 mozilla-unified），`./mach bootstrap` 选桌面开发。
  2. `mozconfig` 固定：`ac_add_options --enable-optimize=-O0`、
     `ac_add_options --disable-debug`、`mk_add_options
     AUTOCLOBBER=1`；编译器指向本仓库构建的 clang（CC/CXX）。
  3. 基线：`./mach build` + `./mach run` 记录基准输出。
  4. 混淆：对目标模块（建议只选安全敏感子集，如
     `security/nss/lib/{crypto,freebl}`、`netwerk/base` 个别文件）在
     源码加 `annotate("vllvm:vmfla")`（C++ 侧宏封装），重跑
     `./mach build <模块>`。
  5. 校验：`./mach test` 抽测 NSS testvectors + 首页加载截图比对；
     性能：`./mach talos` 或页面加载计时对比。
- 门槛：功能测试全过、输出一致；开销记录进报告。

## 第 4 层：结果记录

- 性能数据与结论统一记录 `docs/PASS_PERF_REPORT.md`（含口径与轮次）。
- 缺陷记录同文件「缺陷记录与修复」表；修复必须带复现用例进入 L1。
