#!/usr/bin/env python3
"""常见算法函数级开销基准：每个算法一个独立源文件/可执行文件
（test/perf/workloads/algo/<algo>_bench.c，只含单一算法与官方测试
向量，避免跨算法干扰），逐配置（OBF_MODE 0..6，标注该算法顶层处理
函数）编译为独立二进制计时；跨配置比对摘要一致性。
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
ALGO_DIR = os.path.join(HERE, "workloads", "algo")

# 算法 → 独立源文件（源文件名即算法名）。
ALGOS = ["aes", "md5", "sha256", "des", "sm4", "rc4", "crc32"]

CONFIGS = [
    ("base", 0),
    ("vmfla", 1),
    ("bcf", 2),
    ("bcfvmfla", 3),
    # 组合性能只在算法级测（应用级组合仅做可用性，见 run_perfbench）；
    # 三组组合为 2026-10-20 定版策略，标注集与 run_perfbench 同名配置
    # 一致，且只标注各算法顶层处理函数（不向全部函数施加混淆）。
    ("combo1", 4),
    ("combo2", 5),
    ("combo3", 6),
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
            algo_medians = {}
            for algo in ALGOS:
                src = os.path.join(ALGO_DIR, "%s_bench.c" % algo)
                exe = os.path.join(outdir, "%s_%s_%s" % (algo, name, level))
                cmd = [clang] + sdk + ["-" + level, "-g0",
                                       "-DOBF_MODE=%d" % mode, src,
                                       "-o", exe]
                p = subprocess.run(cmd, capture_output=True, text=True,
                                   timeout=600)
                if p.returncode != 0:
                    sys.exit("compile failed %s/%s -%s: %s"
                             % (algo, name, level, p.stderr[-1500:]))

                # 样本统一 strip 后再计时：-O0 会把 static 函数名留在符号
                # 表，与 run_perfbench/run_python_bench 的发布形态口径一致。
                subprocess.run(["strip", exe], capture_output=True)

                # 每配置跑 runs 次，取轮中位的中位（轮内已带漂移校验）；
                # 单算法可执行只输出自己的 times/digest 两行。
                rounds = []
                dg = None
                for _ in range(max(1, args.runs)):
                    p = subprocess.run([exe], capture_output=True, text=True,
                                       timeout=1800)
                    if p.returncode != 0:
                        sys.exit("run failed %s/%s rc=%d: %s"
                                 % (algo, name, p.returncode, p.stderr[-500:]))
                    for line in p.stdout.splitlines():
                        if " times=" in line:
                            rounds.append(statistics.median(
                                [float(t) for t in
                                 line.split("times=")[1].split(",")]))
                        elif " digest=" in line:
                            dg = line.split("digest=")[1]
                algo_medians[algo] = rounds
                digests.setdefault(algo, {})[name] = dg

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
        for algo in ALGOS:
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
