# test/perf — 开源应用 Pass 性能基准

在开源 C 项目（zlib/lua/sqlite/bzip2）上以全程序函数标注方式测量各 VLLVM Pass
的运行时/体积/编译开销，并校验产物正确性。结果报告见
`docs/PASS_PERF_REPORT.md`。

## 文件

- `run_perfbench.py`：编排脚本（两段式构建 + 计时 + 校验，输出 `results.json`）。
- `vmp_fn_bench.py`：VMP 函数级开销基准（单函数标注探测准入 + 准入形态
  内核微基准，workloads/`vmp_kernels.c` + `vmp_kernel_driver.c`）。
- `inject_vllvm_attrs.py`：向 `-S -emit-llvm` 产物注入 `"vllvm.<kind>"` 函数属性，
  等价于源码 `annotate("vllvm:...")`，支持 `--only`/`--exclude`。
- `tu_bisect.py`：按 TU 单独标注二分定位故障引入者。
- `gen_report.py`：把 `results.json` 汇总为 Markdown 表格。
- `workloads/`：各应用负载（`zbench.c`、`bench.lua`、`bench.sql`）。
- `repro/`：已知缺陷最小复现（icall 跨 TU）。

## 用法

```bash
# 源码放在 test/perf/apps/ 或 tmp/perfbench/apps/：
#   zlib-1.3.1/ bzip2-1.0.8/ lua-5.4.7/ sqlite-amalgamation-3450100/
python3 test/perf/run_perfbench.py --apps bzip2,zlib,lua,sqlite \
  --configs base,fla,bcf,ibr,icall,vmfla,enstr,bb2func,merge,vmp,combo,combo2 \
  --runs 6 --out tmp/perfbench/full

python3 test/perf/gen_report.py tmp/perfbench/full/results.json \
  --output /tmp/report.md --date "$(date +%F)" \
  --machine "$(sysctl -n machdep.cpu.brand_string)" \
  --toolchain "$(./build/llvm-macos/bin/clang --version | head -1)"
```

构建方法与环境注意事项见报告「测试方法」一节（两段式构建等价性、
`-fuse-ld=/usr/bin/ld` 规避 ld64.lld 的 tbd 解析问题）。
