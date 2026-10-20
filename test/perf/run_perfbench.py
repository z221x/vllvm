#!/usr/bin/env python3
"""VLLVM Pass 开源应用性能基准编排。

流程（每个应用 x 每个混淆配置）：
  1. stage1: VLLVM clang -O0 -g0 -S -emit-llvm -Xclang -disable-O0-optnone
     编译每个 TU 得到原始 IR（等价于源码直编时 pipeline-start 时点看到的 IR）。
  2. 用 inject_vllvm_attrs.py 在 IR 的每个 define 上注入 "vllvm.<kind>"
     函数属性（等价于源码 annotate）。
  3. stage2: clang -O0 -g0 -c 编译注入后的 IR（混淆不允许开优化，VLLVM
     start 运行），链接、strip。
  4. 以基线产物为参照做正确性校验，然后计时运行负载（1 次预热 + N 次取中位）。

采集指标：stage2 编译墙钟时间、strip 后二进制大小、__TEXT 段大小、
负载运行时间（中位）、以及每个 TU 的 [vllvm] pass 触发日志。
结果写入 results.json。
"""
import argparse
import json
import math
import re
import os
import shutil
import statistics
import subprocess
import sys
import time
from concurrent.futures import ThreadPoolExecutor

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
APPS_DIR = os.path.join(HERE, "apps")  # 不存在时回退 tmp/perfbench/apps

ZLIB_LIB = ["adler32.c", "compress.c", "crc32.c", "deflate.c", "gzclose.c",
            "gzlib.c", "gzread.c", "gzwrite.c", "infback.c", "inffast.c",
            "inflate.c", "inftrees.c", "trees.c", "uncompr.c", "zutil.c"]
BZIP2_SRC = ["blocksort.c", "bzip2.c", "bzlib.c", "compress.c", "crctable.c",
             "decompress.c", "huffman.c", "randtable.c"]

CONFIGS = {
    "base": [],
    "fla": ["vllvm.fla"],
    "bcf": ["vllvm.bcf"],
    "ibr": ["vllvm.ibr"],
    "icall": ["vllvm.icall"],
    "vmfla": ["vllvm.vmfla"],
    "enstr": ["vllvm.enstr"],
    "bb2func": ["vllvm.bb2func"],
    "merge": ["vllvm.merge"],
    "vmp": ["vllvm.vmp"],
    # bcf 与 vmfla 叠加：调度上 bcf 作用于 bb2func+merge 之后的 wrapper，
    # vmfla 在链路末端平坦化含虚假分支的 CFG。
    "bcfvmfla": ["vllvm.bcf", "vllvm.vmfla"],
    # 组合策略（2026-10-20 定版）：三组组合，不与 bcf 叠加；应用级
    # 组合配合 --annotate-fraction 做选择性标注（不全函数施加混淆）。
    "combo1": ["vllvm.enstr", "vllvm.icall", "vllvm.ibr"],
    "combo2": ["vllvm.vmfla", "vllvm.ibr"],
    "combo3": ["vllvm.enstr", "vllvm.icall", "vllvm.vmfla"],
    # combo1 定界用二元组合（2026-10-20 sqlite/combo1 output-mismatch）
    "pair_enstr_icall": ["vllvm.enstr", "vllvm.icall"],
    "pair_enstr_ibr": ["vllvm.enstr", "vllvm.ibr"],
    "pair_icall_ibr": ["vllvm.icall", "vllvm.ibr"],
}

WARMUP = 1
# 选择性标注：组合策略不向全部函数施加混淆（--annotate-fraction），
# 每 TU 按函数规模（.ll 文本行数）降序取前 N%，至少 1 个。
ANNOTATE_FRACTION = 1.0
DEFINE_RE = re.compile(r"^define\s[^@]*@([-A-Za-z$._][-A-Za-z$._0-9]*)")
TIMED_RUNS = 5
LINK_ARGS_CACHE = []


def _init_link_args():
    global LINK_ARGS_CACHE
    LINK_ARGS_CACHE = link_args()


def find_clang():
    env = os.environ.get("CLANG")
    if env:
        return env
    for p in ("build/llvm-macos/bin/clang", "build/llvm-linux/bin/clang"):
        full = os.path.join(ROOT, p)
        if os.path.isfile(full):
            return full
    return "clang"


def sdk_args():
    if sys.platform == "darwin":
        try:
            sdk = subprocess.run(["xcrun", "--show-sdk-path"],
                                 capture_output=True, text=True,
                                 check=True).stdout.strip()
            if sdk:
                return ["-isysroot", sdk]
        except Exception:
            pass
    return []


def link_args():
    # 自建 clang 默认的 ld64.lld 无法解析新版 macOS SDK 的 tbd
    # （arm64e.x1 等新架构标识），改用系统 ld（shim）链接。
    if sys.platform == "darwin":
        cands = []
        try:
            p = subprocess.run(["xcrun", "-f", "ld-classic"],
                               capture_output=True, text=True)
            if p.returncode == 0:
                cands.append(p.stdout.strip())
        except Exception:
            pass
        cands += ["/usr/bin/ld", shutil.which("ld") or ""]
        for ld in cands:
            if ld and os.path.isfile(ld):
                return ["-fuse-ld=" + ld]
    return []


def app_defs(apps_root):
    """返回 {name: {src_dir, sources, cflags, libs, workload}}。"""
    zbench = os.path.join(HERE, "workloads", "zbench.c")
    bench_lua = os.path.join(HERE, "workloads", "bench.lua")
    bench_sql = os.path.join(HERE, "workloads", "bench.sql")

    def sh(workdir, cmd):
        return {"kind": "sh", "cwd": workdir, "cmd": cmd}

    return {
        "zlib": {
            "src_dir": os.path.join(apps_root, "zlib-1.3.1"),
            "sources": [os.path.join(apps_root, "zlib-1.3.1", s)
                        for s in ZLIB_LIB] + [zbench],
            "cflags": ["-I", os.path.join(apps_root, "zlib-1.3.1"),
                       "-DHAVE_UNISTD_H"],
            "libs": ["-lm"],
            "workload": {"kind": "exe", "argv": ["{exe}"]},
            "desc": "zlib 1.3.1 压缩/解压 32MB x4 轮（zbench 驱动）",
        },
        "bzip2": {
            "src_dir": os.path.join(apps_root, "bzip2-1.0.8"),
            "sources": [os.path.join(apps_root, "bzip2-1.0.8", s)
                        for s in BZIP2_SRC],
            "cflags": [],
            "libs": ["-lm"],
            "workload": sh("app", ['{exe} -9 -c data.bin | md5 && '
                                   '{exe} -d -c data.bin.bz2 | md5']),
            "desc": "bzip2 1.0.8 -9 压缩 + 解压 20MB 文本混合数据",
        },
        "lua": {
            "src_dir": os.path.join(apps_root, "lua-5.4.7", "src"),
            "sources": sorted(
                os.path.join(apps_root, "lua-5.4.7", "src", s)
                for s in os.listdir(os.path.join(apps_root, "lua-5.4.7",
                                                 "src"))
                if s.endswith(".c") and s not in ("luac.c", "onelua.c")),
            "cflags": [],
            "libs": ["-lm"],
            "workload": {"kind": "exe",
                         "argv": ["{exe}", bench_lua]},
            "desc": "lua 5.4.7 解释器跑 n-body + 字符串/表负载",
        },
        "sqlite": {
            "src_dir": os.path.join(apps_root, "sqlite-amalgamation-3450100"),
            "sources": [os.path.join(apps_root, "sqlite-amalgamation-3450100",
                                     "sqlite3.c"),
                        os.path.join(apps_root,
                                     "sqlite-amalgamation-3450100",
                                     "shell.c")],
            "cflags": ["-DSQLITE_THREADSAFE=0",
                       "-DSQLITE_OMIT_LOAD_EXTENSION",
                       "-DSQLITE_ENABLE_MATH_FUNCTIONS"],
            "libs": ["-lm"],
            "workload": {"kind": "exe",
                         "argv": ["{exe}", ":memory:",
                                  ".read {sql}"]},
            "desc": "sqlite 3.45.1 CLI 内存库 15 万行建库/索引/聚合",
        },
    }


def tu_name(src):
    return os.path.splitext(os.path.basename(src))[0]


def run_cmd(argv, cwd=None, timeout=None, capture=True):
    t0 = time.perf_counter()
    p = subprocess.run(argv, cwd=cwd, timeout=timeout,
                       capture_output=capture, text=True)
    dt = time.perf_counter() - t0
    return p, dt


class BenchError(Exception):
    pass


def stage1_compile(clang, sdk, app, spec, outdir, jobs, log):
    os.makedirs(outdir, exist_ok=True)

    def one(src):
        ll = os.path.join(outdir, tu_name(src) + ".ll")
        p, dt = run_cmd([clang] + sdk + ["-O0", "-g0", "-S", "-emit-llvm",
                                         "-Xclang", "-disable-O0-optnone"]
                        + spec["cflags"] + ["-c", src, "-o", ll],
                        timeout=900)
        if p.returncode != 0:
            raise BenchError("stage1 failed %s: %s" % (src, p.stderr[-2000:]))
        return ll, dt

    with ThreadPoolExecutor(max_workers=jobs) as ex:
        results = list(ex.map(one, spec["sources"]))
    log("stage1 %s done: %d TUs" % (app, len(results)))
    return [r[0] for r in results]


def function_sizes(ll_path):
    sizes = {}
    name = None
    with open(ll_path) as f:
        for line in f:
            if name is None:
                m = DEFINE_RE.match(line)
                if m:
                    name = m.group(1)
                    sizes[name] = 0
            elif line.startswith("}"):
                name = None
            else:
                sizes[name] += 1
    return sizes


def select_functions(ll):
    if ANNOTATE_FRACTION >= 1.0:
        return None
    sizes = function_sizes(ll)
    ranked = sorted(sizes, key=lambda n: sizes[n], reverse=True)
    keep = max(1, math.ceil(len(ranked) * ANNOTATE_FRACTION))
    return ranked[:keep]


def build_config(clang, sdk, app, spec, config, attrs, lls, workdir, jobs,
                 log, tu_timeout):
    os.makedirs(workdir, exist_ok=True)
    injected = []
    for ll in lls:
        dst = os.path.join(workdir, tu_name(ll) + ".ll")
        if attrs:
            cmd = [sys.executable,
                   os.path.join(HERE, "inject_vllvm_attrs.py"),
                   ll, dst, "--attrs", ",".join(attrs)]
            only = select_functions(ll)
            if only is not None:
                cmd += ["--only", ",".join(only)]
            p = subprocess.run(cmd, capture_output=True, text=True,
                               timeout=120)
            if p.returncode != 0:
                raise BenchError("inject failed %s: %s" % (ll, p.stderr))
        else:
            shutil.copyfile(ll, dst)
        injected.append(dst)

    tu_times = {}
    pass_log = {}

    # VLLVM_CONFIG 环境变量（如 "icall=2"）转发 -mllvm -vllvm-config=
    config_args = []
    if os.environ.get("VLLVM_CONFIG"):
        config_args = ["-mllvm", "-vllvm-config=" + os.environ["VLLVM_CONFIG"]]

    def compile_one(ll):
        obj = os.path.join(workdir, tu_name(ll) + ".o")
        t0 = time.perf_counter()
        try:
            p = subprocess.run([clang] + sdk + config_args +
                               ["-O0", "-g0", "-c", ll, "-o", obj],
                               capture_output=True, text=True,
                               timeout=tu_timeout)
        except subprocess.TimeoutExpired:
            raise BenchError("stage2 timeout (>%.0fs) on %s"
                             % (tu_timeout, ll))
        dt = time.perf_counter() - t0
        if p.returncode != 0:
            raise BenchError("stage2 failed %s: %s" % (ll, p.stderr[-3000:]))
        tu_times[tu_name(ll)] = round(dt, 2)
        pass_log[tu_name(ll)] = [l for l in p.stderr.splitlines()
                                 if l.startswith("[vllvm]")]
        return obj

    t0 = time.perf_counter()
    with ThreadPoolExecutor(max_workers=jobs) as ex:
        objs = list(ex.map(compile_one, injected))
    build_s = time.perf_counter() - t0

    exe = os.path.join(workdir, "app.bin")
    p, _ = run_cmd([clang] + sdk + LINK_ARGS_CACHE + ["-O2", "-g0"] + objs
                   + spec["libs"] + ["-o", exe], timeout=600)
    if p.returncode != 0:
        raise BenchError("link failed: %s" % p.stderr[-2000:])
    subprocess.run(["strip", exe], capture_output=True)

    size_bytes = os.path.getsize(exe)
    text_bytes = None
    p, _ = run_cmd(["size", exe])
    if p.returncode == 0:
        for line in p.stdout.splitlines():
            parts = line.split()
            # macOS BSD size: 表头 __TEXT __DATA __OBJC others dec hex，
            # 数据行第一列即 __TEXT 字节数
            if len(parts) >= 4 and parts[0] != "__TEXT":
                try:
                    text_bytes = int(parts[0])
                    break
                except ValueError:
                    continue
    log("build %s/%s: %.1fs  %d bytes" % (app, config, build_s, size_bytes))
    return {
        "exe": exe, "build_s": round(build_s, 2),
        "tu_times": tu_times, "pass_log": pass_log,
        "size_bytes": size_bytes, "text_bytes": text_bytes,
    }


def workload_argv(workload, exe, app_dir):
    if workload["kind"] == "exe":
        return [a.replace("{exe}", exe).replace("{sql}",
                os.path.join(HERE, "workloads", "bench.sql"))
                for a in workload["argv"]], None
    cmd = " ".join(workload["cmd"]).replace("{exe}", exe)
    return ["sh", "-c", cmd], app_dir


def run_workload(spec, exe, timeout, app_dir=None):
    argv, cwd = workload_argv(spec["workload"], exe, app_dir)
    p, dt = run_cmd(argv, cwd=cwd, timeout=timeout)
    return p, dt


def bench_app(clang, sdk, name, spec, apps_dir, out_root, jobs, runs,
              configs, tu_timeout, log, run_timeout):
    outdir = os.path.join(out_root, name)
    os.makedirs(outdir, exist_ok=True)
    lls = stage1_compile(clang, sdk, name, spec,
                         os.path.join(outdir, "stage1"), jobs, log)

    if name == "bzip2":
        # 造一份确定性 20MB 测试数据（文本块 + 伪随机块各半，避免
        # 高度可压缩导致压缩耗时失真）
        import random as _random
        data = os.path.join(outdir, "data.bin")
        if not os.path.isfile(data):
            rng = _random.Random(20241009)
            chunk_txt = (b"The quick brown fox jumps over the lazy dog. "
                         b"vllvm perf sample line %d\n")
            with open(data, "wb") as f:
                total = 0
                i = 0
                while total < 20 << 20:
                    if rng.getrandbits(1):
                        b = chunk_txt % (i,)
                    else:
                        b = rng.randbytes(2048)
                    f.write(b)
                    total += len(b)
                    i += 1
        spec = dict(spec)
        spec["workdir_data"] = data

    results = {}
    reference = None
    # 参照必须来自 base：补测单配置时也先构建 base，避免用被测配置
    # 自身的输出当参照（坏产物会“自我一致”）
    run_configs = list(configs)
    if run_configs and run_configs[0] != "base":
        run_configs.insert(0, "base")
    for config in run_configs:
        if config not in configs and reference is not None:
            continue  # base 只用于建参照，不进结果
        attrs = CONFIGS[config]
        workdir = os.path.join(outdir, config)
        try:
            built = build_config(clang, sdk, name, spec, config, attrs, lls,
                                 workdir, jobs, log, tu_timeout)
        except BenchError as e:
            log("build FAILED %s/%s: %s" % (name, config, e))
            results[config] = {"status": "build-failed", "error": str(e)}
            continue

        exe = built["exe"]
        # bzip2 压缩参照文件用系统 bzip2 生成，避免依赖被测配置自身
        if name == "bzip2" and not os.path.isfile(
                os.path.join(outdir, "data.bin.bz2")):
            sysbz = shutil.which("bzip2") or exe
            subprocess.run([sysbz, "-9", "-k",
                            os.path.join(outdir, "data.bin")],
                           cwd=outdir, check=True, capture_output=True)

        # 正确性：预热运行输出需与基线一致
        try:
            p, _ = run_workload(spec, exe, run_timeout, app_dir=outdir)
        except subprocess.TimeoutExpired:
            log("run TIMEOUT %s/%s" % (name, config))
            results[config] = {"status": "run-timeout"}
            continue
        ok = p.returncode == 0 and (reference is None or
                                    p.stdout == reference)
        if reference is None:
            reference = p.stdout
            if p.returncode != 0:
                results[config] = {"status": "bad-exit",
                                   "rc": p.returncode,
                                   "stderr": p.stderr[-500:]}
                continue

        times = []
        for _ in range(runs):
            p2, dt = run_workload(spec, exe, run_timeout, app_dir=outdir)
            if p2.returncode != 0 or p2.stdout != reference:
                ok = False
                break
            times.append(dt)
        if not times:
            if runs == 0:
                # 可用性模式（--runs 0）：编译成功 + 预热那次执行即为
                # 验证（退出码 0 且输出与基线一致），不计时。
                results[config] = {
                    "status": "ok" if ok else "output-mismatch",
                    "build_s": built["build_s"],
                    "size_bytes": built["size_bytes"],
                }
                log("run %s/%s: usability ok=%s" % (name, config, ok))
                continue
            results[config] = {"status": "flaky", "rc": p2.returncode}
            continue

        results[config] = {
            "status": "ok" if ok else "output-mismatch",
            "build_s": built["build_s"],
            "tu_times": built["tu_times"],
            "size_bytes": built["size_bytes"],
            "text_bytes": built["text_bytes"],
            "runs": [round(t, 4) for t in times],
            "median_s": round(statistics.median(times), 4),
            "min_s": round(min(times), 4),
            "vllvm_log": {k: v[:6] for k, v in built["pass_log"].items()},
        }
        log("run %s/%s: median %.3fs ok=%s"
            % (name, config, statistics.median(times), ok))

    return results


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--apps", default="zlib,bzip2,lua,sqlite")
    ap.add_argument("--configs",
                    default="base,fla,bcf,ibr,icall,vmfla,enstr,bb2func,"
                            "merge,vmp,combo")
    ap.add_argument("--runs", type=int, default=WARMUP + TIMED_RUNS,
                    help="计时执行次数；0 = 可用性模式（编译成功 + 单次"
                         "执行输出与基线一致即通过，不计时）")
    ap.add_argument("--jobs", type=int, default=6)
    ap.add_argument("--annotate-fraction", type=float, default=1.0,
                    help="标注函数比例（0<frac<=1）：每 TU 按函数规模降序"
                         "取前 N%%，组合策略不全函数施加混淆时使用")
    ap.add_argument("--tu-timeout", type=float, default=1800)
    ap.add_argument("--run-timeout", type=float, default=900)
    ap.add_argument("--out", default=None)
    args = ap.parse_args()
    global ANNOTATE_FRACTION
    ANNOTATE_FRACTION = min(1.0, max(0.01, args.annotate_fraction))

    clang = find_clang()
    sdk = sdk_args()
    _init_link_args()
    apps_root = APPS_DIR if os.path.isdir(APPS_DIR) else os.path.join(
        ROOT, "tmp", "perfbench", "apps")
    defs = app_defs(apps_root)
    out_root = os.path.abspath(args.out or os.path.join(HERE, "out"))
    os.makedirs(out_root, exist_ok=True)

    log_file = open(os.path.join(out_root, "bench.log"), "a")

    def log(msg):
        line = "[%s] %s" % (time.strftime("%H:%M:%S"), msg)
        print(line, flush=True)
        log_file.write(line + "\n")
        log_file.flush()

    all_results = {}
    for name in args.apps.split(","):
        if name not in defs:
            sys.exit("unknown app: %s" % name)
        configs = [c for c in args.configs.split(",") if c]
        for c in configs:
            if c not in CONFIGS:
                sys.exit("unknown config: %s" % c)
        log("=== app %s (%s) configs=%s clang=%s"
            % (name, defs[name]["desc"], ",".join(configs), clang))
        all_results[name] = bench_app(clang, sdk, name, defs[name],
                                      apps_root, out_root, args.jobs,
                                      args.runs, configs, args.tu_timeout,
                                      log, args.run_timeout)

    with open(os.path.join(out_root, "results.json"), "w") as f:
        json.dump(all_results, f, indent=1, ensure_ascii=False)
    log("results written to %s" % os.path.join(out_root, "results.json"))


if __name__ == "__main__":
    main()
