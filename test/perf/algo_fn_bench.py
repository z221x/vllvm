#!/usr/bin/env python3
"""常见算法（AES-128-CBC / MD5 / SHA-256）函数级开销基准：
test/perf/workloads/algo_bench.c 同源编译 4 配置（OBF_MODE 0..3，
标注各算法顶层处理函数），二进制内逐轮计时；跨配置比对摘要一致性。
用法：python3 test/perf/algo_fn_bench.py [--runs N] [--out DIR]
"""
import argparse
import json
import os
import statistics
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
SRC = os.path.join(HERE, "workloads", "algo_bench.c")

CONFIGS = [
    ("base", 0),
    ("vmfla", 1),
    ("bcf", 2),
    ("bcfvmfla", 3),
]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--runs", type=int, default=1,
                    help="二进制重复执行次数（轮内已多次采样）")
    ap.add_argument("--out", default=None)
    ap.add_argument("--opt", default="O0",
                    help="逗号分隔的优化级别（如 O1,O0），逐级编译运行")
    args = ap.parse_args()

    clang = os.environ.get("CLANG") or os.path.join(
        ROOT, "build/llvm-macos/bin/clang")
    sdk = []
    if sys.platform == "darwin":
        p = subprocess.run(["xcrun", "--show-sdk-path"],
                           capture_output=True, text=True)
        if p.returncode == 0 and p.stdout.strip():
            sdk = ["-isysroot", p.stdout.strip()]

    outdir = args.out or os.path.join(ROOT, "tmp/perfbench/algo-vmfla")
    os.makedirs(outdir, exist_ok=True)

    def run_level(level):
        results = {}
        digests = {}
        for name, mode in CONFIGS:
            exe = os.path.join(outdir, "algo_%s_%s" % (name, level))
            cmd = [clang] + sdk + ["-" + level, "-g0",
                                   "-DOBF_MODE=%d" % mode, SRC, "-o", exe]
            p = subprocess.run(cmd, capture_output=True, text=True,
                               timeout=600)
            if p.returncode != 0:
                sys.exit("compile failed %s -%s: %s"
                         % (name, level, p.stderr[-1500:]))

            # 样本统一 strip 后再计时：-O0 会把 static 函数名留在符号
            # 表，与 run_perfbench/run_python_bench 的发布形态口径一致。
            subprocess.run(["strip", exe], capture_output=True)

            # 每配置跑 runs 次，取轮中位的中位（轮内已带漂移校验）。
            algo_medians = {}
            for _ in range(max(1, args.runs)):
                p = subprocess.run([exe], capture_output=True, text=True,
                                   timeout=1800)
                if p.returncode != 0:
                    sys.exit("run failed %s rc=%d: %s"
                             % (name, p.returncode, p.stderr[-500:]))
                cur = {}
                dg = {}
                for line in p.stdout.splitlines():
                    # 行格式："<algo> times=t1,t2,..." 与 "<algo> digest=hex"，
                    # 算法名由 fixture 动态决定。
                    if " times=" in line:
                        algo = line.split()[0]
                        cur[algo] = [float(t) for t in
                                     line.split("times=")[1].split(",")]
                    elif " digest=" in line:
                        algo = line.split()[0]
                        dg[algo] = line.split("digest=")[1]
                for algo, ts in cur.items():
                    algo_medians.setdefault(algo, []).append(
                        statistics.median(ts))
                for algo, d in dg.items():
                    digests.setdefault(algo, {})[name] = d

            results[name] = {a: round(statistics.median(ms), 6)
                             for a, ms in algo_medians.items()}
            print("[algo -%s] %-9s %s" % (level, name, results[name]))

        # 摘要跨配置必须一致
        for algo, per in digests.items():
            vals = set(per.values())
            if len(vals) != 1:
                sys.exit("digest mismatch for %s -%s: %s"
                         % (algo, level, per))

        base = results["base"]
        print("\n[-%s] %-8s" % (level, "algo"),
              "".join("%12s" % n for n, _ in CONFIGS))
        ratios = {}
        for algo in sorted(base):
            ratios[algo] = {}
            cells = []
            for name, _ in CONFIGS:
                r = results[name][algo] / base[algo]
                ratios[algo][name] = round(r, 2)
                cells.append("%11.2fx" % r)
            print("%-8s" % algo, "".join(cells))

        path = os.path.join(outdir, "results_%s.json" % level)
        with open(path, "w") as f:
            json.dump({"median_s": results, "ratio": ratios,
                       "digests": {a: list(p.values())[0]
                                   for a, p in digests.items()}},
                      f, indent=1, ensure_ascii=False)
        print("results -> %s\n" % path)

    for level in [l.strip() for l in args.opt.split(",") if l.strip()]:
        run_level(level)


if __name__ == "__main__":
    main()
