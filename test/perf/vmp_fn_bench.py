#!/usr/bin/env python3
"""VMP 函数级开销基准：对开源项目中的候选函数逐个单独标注 vllvm:vmp，
先探测准入（.o 中出现 __vllvm_vmp_code.<fn>），再对通过者用微基准
驱动测量混淆前后的每调用开销。"""
import argparse
import os
import shutil
import statistics
import subprocess
import sys
import time
from concurrent.futures import ThreadPoolExecutor

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
APPS = os.path.join(ROOT, "tmp", "perfbench", "apps")
CLANG = os.path.join(ROOT, "build/llvm-macos/bin/clang")
SDK = subprocess.run(["xcrun", "--show-sdk-path"], capture_output=True,
                     text=True, check=True).stdout.strip()
INJ = os.path.join(HERE, "inject_vllvm_attrs.py")
OUT = os.path.join(ROOT, "tmp", "perfbench", "vmp_fn")

ZLIB_LIB = ["adler32.c", "compress.c", "crc32.c", "deflate.c", "gzclose.c",
            "gzlib.c", "gzread.c", "gzwrite.c", "infback.c", "inffast.c",
            "inflate.c", "inftrees.c", "trees.c", "uncompr.c", "zutil.c"]
BZIP2_LIB = ["blocksort.c", "bzlib.c", "compress.c", "crctable.c",
             "decompress.c", "huffman.c", "randtable.c"]
LUA_LIB = sorted(
    s for s in os.listdir(os.path.join(APPS, "lua-5.4.7", "src"))
    if s.endswith(".c") and s not in ("luac.c", "onelua.c", "lua.c"))

DRIVERS = {
    "zlib_adler32": """
#include <stddef.h>
unsigned long adler32(unsigned long, const unsigned char *, unsigned);
int main(void) {
  static unsigned char buf[1 << 20];
  for (size_t i = 0; i < sizeof buf; i++)
    buf[i] = (unsigned char)((i * 2654435761u) >> 8);
  unsigned long h = 1;
  for (int r = 0; r < RUNS; r++)
    h = adler32(h, buf, (unsigned)sizeof buf);
  return h == 0;
}
""",
    "zlib_crc32": """
#include <stddef.h>
unsigned long crc32(unsigned long, const unsigned char *, unsigned);
int main(void) {
  static unsigned char buf[1 << 20];
  for (size_t i = 0; i < sizeof buf; i++)
    buf[i] = (unsigned char)((i * 40503u) >> 6);
  unsigned long h = 0;
  for (int r = 0; r < RUNS; r++)
    h = crc32(h, buf, (unsigned)sizeof buf);
  return h == 0;
}
""",
    "zlib_crc32_z": """
#include <stddef.h>
unsigned long crc32(unsigned long, const unsigned char *, unsigned);
size_t crc32_z(unsigned long, const unsigned char *, size_t);
int main(void) {
  static unsigned char buf[1 << 20];
  for (size_t i = 0; i < sizeof buf; i++)
    buf[i] = (unsigned char)((i * 40503u) >> 6);
  unsigned long h = 0;
  for (int r = 0; r < RUNS; r++)
    h = crc32_z(h, buf, sizeof buf);
  return h == 0;
}
""",
    "zlib_deflate": """
#include <stddef.h>
#include <string.h>
#include "zlib.h"
int main(void) {
  static unsigned char in[1 << 20], out[1 << 20 + 8];
  for (size_t i = 0; i < sizeof in; i++)
    in[i] = (unsigned char)((i * 2654435761u) >> 8);
  for (int r = 0; r < RUNS; r++) {
    z_stream s;
    memset(&s, 0, sizeof s);
    if (deflateInit2(&s, 6, Z_DEFLATED, 15, 8, Z_DEFAULT_STRATEGY) != Z_OK)
      return 9;
    s.next_in = in; s.avail_in = sizeof in;
    s.next_out = out; s.avail_out = sizeof out;
    deflate(&s, Z_FINISH);
    deflateEnd(&s);
  }
  return 0;
}
""",
    "bzip2_hbAssignCodes": """
void BZ2_hbAssignCodes(int *, unsigned char *, int, int, int);
int main(void) {
  int code[512];
  unsigned char len[512];
  for (int i = 0; i < 512; i++)
    len[i] = (unsigned char)(1 + i % 9);
  for (int r = 0; r < RUNS; r++)
    BZ2_hbAssignCodes(code, len, 1, 9, 23);
  return 0;
}
""",
    "bzip2_hbMakeCodeLengths": """
void BZ2_hbMakeCodeLengths(unsigned char *, int *, int, int);
int main(void) {
  unsigned char len[512];
  int freq[512];
  for (int i = 0; i < 512; i++)
    freq[i] = (i * 2654435761u) >> 16;
  for (int r = 0; r < RUNS; r++)
    BZ2_hbMakeCodeLengths(len, freq, 511, 17);
  return 0;
}
""",
    "lua_luaV_idiv": """
long long luaV_idiv(long long, long long);
int main(void) {
  long long a = 12345678901234567LL, b = 97531LL, s = 0;
  for (int r = 0; r < RUNS; r++)
    s += luaV_idiv(a + r, b);
  return s == 0;
}
""",
    "lua_luaV_mod": """
long long luaV_mod(long long, long long, int);
int main(void) {
  long long a = 12345678901234567LL, b = 97531LL, s = 0;
  for (int r = 0; r < RUNS; r++)
    s += luaV_mod(a + r, b, 1);
  return s == 0;
}
""",
}

# (项目, TU 源文件, 函数名, 驱动 key, RUNS, 额外 cflags)
# kernels 项目为单段直编的合成准入形态内核（源码级标注），驱动用
# KERNEL=1..5 选择；RUNS 为驱动外层循环、ITER 为内核内层循环。
KERNEL_CANDIDATES = [
    ("kernels", "k_leaf", 1, 20000000, 0),
    ("kernels", "k_loop", 2, 200, 1000000),
    ("kernels", "k_branch", 3, 20000000, 0),
    ("kernels", "k_mem", 4, 200, 1000000),
    ("kernels", "k_call", 5, 200, 1000000),
]

CANDIDATES = [
    ("zlib", "adler32.c", "adler32", "zlib_adler32", 600,
     ["-I", os.path.join(APPS, "zlib-1.3.1"), "-DHAVE_UNISTD_H"]),
    ("zlib", "crc32.c", "crc32", "zlib_crc32", 2000,
     ["-I", os.path.join(APPS, "zlib-1.3.1"), "-DHAVE_UNISTD_H"]),
    ("zlib", "crc32.c", "crc32_z", "zlib_crc32_z", 2000,
     ["-I", os.path.join(APPS, "zlib-1.3.1"), "-DHAVE_UNISTD_H"]),
    ("zlib", "deflate.c", "deflate", "zlib_deflate", 200,
     ["-I", os.path.join(APPS, "zlib-1.3.1"), "-DHAVE_UNISTD_H"]),
    ("zlib", "trees.c", "_tr_flush_block", None, 0,
     ["-I", os.path.join(APPS, "zlib-1.3.1"), "-DHAVE_UNISTD_H"]),
    ("bzip2", "huffman.c", "BZ2_hbAssignCodes", "bzip2_hbAssignCodes",
     3000000, []),
    ("bzip2", "huffman.c", "BZ2_hbMakeCodeLengths", "bzip2_hbMakeCodeLengths",
     2000000, []),
    ("lua", "lvm.c", "luaV_idiv", "lua_luaV_idiv", 30000000, []),
    ("lua", "lvm.c", "luaV_mod", "lua_luaV_mod", 30000000, []),
    ("lua", "lvm.c", "luaV_tointegerns", None, 0, []),
]


def build_kernels(fn, kernel_id, runs, iters, workdir):
    """合成内核单段直编：vmp 版加 -DKERNELS_VMP，base 不加。"""
    os.makedirs(workdir, exist_ok=True)
    kernels = os.path.join(HERE, "workloads", "vmp_kernels.c")
    driver = os.path.join(HERE, "workloads", "vmp_kernel_driver.c")
    exes = {}
    ko = os.path.join(workdir, "kernels.o")
    r = subprocess.run([CLANG, "-isysroot", SDK, "-O2", "-g0",
                        "-DKERNELS_VMP", "-c", kernels, "-o", ko],
                       capture_output=True, text=True)
    assert r.returncode == 0, r.stderr[-800:]
    admitted = ("__vllvm_vmp_code.%s" % fn) in subprocess.run(
        ["nm", ko], capture_output=True, text=True).stdout
    if not admitted:
        return None, None, admitted
    for tag, extra in (("base", []), ("vmp", ["-DKERNELS_VMP"])):
        exe = os.path.join(workdir, "bench_%s" % tag)
        r = subprocess.run([CLANG, "-isysroot", SDK,
                            "-fuse-ld=/usr/bin/ld", "-O2", "-g0"]
                           + extra +
                           ["-DKERNEL=%d" % kernel_id, "-DRUNS=%d" % runs,
                            "-DITER=%d" % iters, kernels, driver,
                            "-lm", "-o", exe],
                           capture_output=True, text=True)
        assert r.returncode == 0, r.stderr[-800:]
        exes[tag] = exe
    return exes["base"], exes["vmp"], admitted


def project_tus(project):
    if project == "zlib":
        return [os.path.join(APPS, "zlib-1.3.1", s) for s in ZLIB_LIB]
    if project == "bzip2":
        return [os.path.join(APPS, "bzip2-1.0.8", s) for s in BZIP2_LIB]
    return [os.path.join(APPS, "lua-5.4.7", "src", s) for s in LUA_LIB]


def build_pair(project, tu_name, fn, driver_key, runs, cflags, workdir):
    """构建 base 与 vmp（仅 fn 标注）两个二进制；返回 (base_exe, vmp_exe,
    admitted)。"""
    os.makedirs(workdir, exist_ok=True)
    stage1 = os.path.join(workdir, "stage1")
    os.makedirs(stage1, exist_ok=True)
    tus = project_tus(project)

    def compile_one(src):
        ll = os.path.join(stage1, os.path.splitext(os.path.basename(src))[0]
                          + ".ll")
        r = subprocess.run([CLANG, "-isysroot", SDK, "-O0", "-g0", "-S",
                            "-emit-llvm", "-Xclang", "-disable-O0-optnone"]
                           + cflags + ["-c", src, "-o", ll],
                           capture_output=True, text=True)
        assert r.returncode == 0, r.stderr[-500:]
        return ll

    # stage1 复用：同项目候选共享
    lls = []
    todo = [s for s in tus
            if not os.path.exists(os.path.join(
                stage1, os.path.splitext(os.path.basename(s))[0] + ".ll"))]
    with ThreadPoolExecutor(max_workers=8) as ex:
        ex.map(compile_one, todo)
    for src in tus:
        lls.append(os.path.join(
            stage1, os.path.splitext(os.path.basename(src))[0] + ".ll"))

    objs = []
    for ll in lls:
        name = os.path.splitext(os.path.basename(ll))[0]
        inj = os.path.join(workdir, name + ".vmp.ll") if name == \
            os.path.splitext(tu_name)[0] else None
        src_ll = inj if inj else ll
        if inj:
            r = subprocess.run([sys.executable, INJ, ll, inj,
                                "--attrs", "vllvm.vmp", "--only", fn],
                               capture_output=True, text=True)
            assert r.returncode == 0, r.stderr
        obj = os.path.join(workdir, name + ".o")
        r = subprocess.run([CLANG, "-isysroot", SDK, "-O2", "-g0", "-c",
                            src_ll, "-o", obj],
                           capture_output=True, text=True)
        assert r.returncode == 0, r.stderr[-500:]
        objs.append(obj)

    admitted = False
    tu_obj = os.path.join(workdir, os.path.splitext(tu_name)[0] + ".o")
    r = subprocess.run(["nm", tu_obj], capture_output=True, text=True)
    admitted = ("__vllvm_vmp_code.%s" % fn) in r.stdout

    if not driver_key:
        return None, None, admitted

    exes = {}
    for tag in ("base", "vmp"):
        if tag == "base":
            # base 复用未注入 TU 的对象即可，但对象已带 vmp —— 重编 base
            # 对象（无注入）
            tu_base = os.path.join(workdir, "base." +
                                   os.path.splitext(tu_name)[0] + ".o")
            src_ll = os.path.join(
                stage1, os.path.splitext(tu_name)[0] + ".ll")
            r = subprocess.run([CLANG, "-isysroot", SDK, "-O2", "-g0", "-c",
                                src_ll, "-o", tu_base],
                               capture_output=True, text=True)
            assert r.returncode == 0, r.stderr[-500:]
            link_objs = [tu_base] + [o for o in objs if not
                                     o.endswith(os.path.splitext(tu_name)[0]
                                                + ".o")]
        else:
            link_objs = objs
        exe = os.path.join(workdir, "bench_%s" % tag)
        r = subprocess.run([CLANG, "-isysroot", SDK, "-fuse-ld=/usr/bin/ld",
                            "-O2", "-g0"] + link_objs +
                           [os.path.join(workdir, "driver.c"),
                            "-DRUNS=%d" % runs, "-lm", "-o", exe],
                           capture_output=True, text=True)
        assert r.returncode == 0, r.stderr[-500:]
        exes[tag] = exe
    return exes["base"], exes["vmp"], admitted


def timeit(exe, runs_measure=3):
    ts = []
    for _ in range(runs_measure):
        t0 = time.perf_counter()
        p = subprocess.run([exe], capture_output=True)
        assert p.returncode == 0, "bench run failed: %s" % exe
        ts.append(time.perf_counter() - t0)
    return statistics.median(ts)


# 标准算法矩阵：ALGO id → 驱动选择；每项给出 base/vmp 各自的 RUNS、
# 每轮包含的虚拟化单元数（块/字/调用）。
ALGO_CANDIDATES = [
    ("fib-recursive-n32", 1, "fib_recursive", 40, 2, 1, "call"),
    ("sha256-4kb", 2, "sha256_block", 4000, 50, 64, "block"),
    ("fib-iterative-n92", 3, "fib_iterative", 5000000, 30000, 1, "call"),
    ("sha1-4kb", 4, "sha1_block", 5000, 50, 64, "block"),
    ("sha256-64b", 5, "sha256_block", 300000, 5000, 1, "block"),
    ("md5-4kb", 6, "md5_block", 5000, 50, 64, "block"),
    ("md5-64b", 7, "md5_block", 300000, 5000, 1, "block"),
    ("sha1-64b", 8, "sha1_block", 300000, 5000, 1, "block"),
    ("crc32-64b", 9, "crc32_word", 600000, 3000, 8, "word"),
    ("crc32-4kb", 10, "crc32_word", 2500, 40, 512, "word"),
]


def build_algos(name, algo_id, fn, base_runs, vmp_runs, workdir):
    """算法矩阵构建：vmp 版 -DALGOS_VMP；base/vmp 各自 RUNS。"""
    os.makedirs(workdir, exist_ok=True)
    algos = os.path.join(HERE, "workloads", "vmp_algos.c")
    driver = os.path.join(HERE, "workloads", "vmp_algo_driver.c")
    ko = os.path.join(workdir, "algos.o")
    r = subprocess.run([CLANG, "-isysroot", SDK, "-O2", "-g0",
                        "-fno-vectorize", "-fno-slp-vectorize",
                        "-DALGOS_VMP", "-c", algos, "-o", ko],
                       capture_output=True, text=True)
    assert r.returncode == 0, r.stderr[-800:]
    admitted = ("__vllvm_vmp_code.%s" % fn) in subprocess.run(
        ["nm", ko], capture_output=True, text=True).stdout
    exes = {}
    for tag, extra, runs in (("base", [], base_runs),
                             ("vmp", ["-DALGOS_VMP"], vmp_runs)):
        exe = os.path.join(workdir, "bench_%s" % tag)
        r = subprocess.run([CLANG, "-isysroot", SDK, "-fuse-ld=/usr/bin/ld",
                            "-O2", "-g0", "-fno-vectorize",
                            "-fno-slp-vectorize", "-DALGO=%d" % algo_id,
                            "-DRUNS=%d" % runs] + extra +
                           [algos, driver, "-lm", "-o", exe],
                           capture_output=True, text=True)
        assert r.returncode == 0, r.stderr[-800:]
        exes[tag] = (exe, runs)
    return exes, admitted


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", default=OUT)
    args = ap.parse_args()
    os.makedirs(args.out, exist_ok=True)

    results = []
    for name, algo_id, fn, bru, vru, units, unit_name in ALGO_CANDIDATES:
        wd = os.path.join(args.out, "algo.%s" % name)
        exes, admitted = build_algos(name, algo_id, fn, bru, vru, wd)
        row = {"algo": name, "admitted": admitted}
        if admitted:
            try:
                tb = timeit(exes["base"][0])
                tv = timeit(exes["vmp"][0])
            except (AssertionError, subprocess.TimeoutExpired):
                row["status"] = "vmp-crash"
                results.append(row)
                print("%-20s admitted vmp-CRASH（虚拟化后运行崩溃）" % name,
                      flush=True)
                continue
            row.update(base_ns_unit=round(tb * 1e9 / (bru * units), 1),
                       vmp_ns_unit=round(tv * 1e9 / (vru * units), 1),
                       ratio=round(tv / vru / (tb / bru), 1))
            results.append(row)
            print("%-20s admitted base %.1fns/%s vmp %.1fns/%s = %.1fx" % (
                name, row["base_ns_unit"], unit_name,
                row["vmp_ns_unit"], unit_name, row["ratio"]), flush=True)
        else:
            results.append(row)
            print("%-20s FALLBACK" % name, flush=True)

    for project, fn, kid, runs, iters in KERNEL_CANDIDATES:
        wd = os.path.join(args.out, "kernels.%s" % fn)
        base, vmp, admitted = build_kernels(fn, kid, runs, iters, wd)
        row = {"project": "kernels", "fn": fn, "admitted": admitted}
        if admitted:
            tb = timeit(base)
            tv = timeit(vmp)
            row.update(base_s=round(tb, 4), vmp_s=round(tv, 4),
                       ratio=round(tv / tb, 2),
                       ns_per_call=round(tv * 1e9 / runs, 1),
                       ns_per_iter=(round(tv * 1e9 / (runs * iters), 1)
                                    if iters else None))
        results.append(row)
        if admitted:
            desc = "base %.3fs vmp %.3fs = %.2fx, %.1f ns/call%s" % (
                row.get("base_s", 0), row.get("vmp_s", 0),
                row.get("ratio", 0), row.get("ns_per_call", 0),
                (", %.1f ns/iter" % row["ns_per_iter"])
                if row.get("ns_per_iter") else "")
        else:
            desc = "(fallback)"
        print("kernels.%-12s admitted=%-5s %s" % (fn, admitted, desc),
              flush=True)

    for project, tu, fn, driver, runs, cflags in CANDIDATES:
        tag = "%s.%s" % (project, fn)
        wd = os.path.join(args.out, tag)
        if driver:
            drv = os.path.join(wd, "driver.c")
            os.makedirs(wd, exist_ok=True)
            with open(drv, "w") as f:
                f.write(DRIVERS[driver])
        base, vmp, admitted = build_pair(project, tu, fn, driver, runs,
                                         cflags, wd)
        row = {"project": project, "fn": fn, "admitted": admitted}
        if admitted and driver:
            tb = timeit(base)
            tv = timeit(vmp)
            row.update(base_s=round(tb, 4), vmp_s=round(tv, 4),
                       ratio=round(tv / tb, 2),
                       ns_per_call=round(tv * 1e9 / runs, 1))
        results.append(row)
        print("%-28s admitted=%-5s %s" % (
            tag, admitted,
            ("base %.3fs vmp %.3fs = %.2fx, %.1f ns/call"
             % (row.get("base_s", 0), row.get("vmp_s", 0),
                row.get("ratio", 0), row.get("ns_per_call", 0)))
            if admitted and driver else "(fallback 或无驱动，仅探测)",
        ), flush=True)

    import json
    with open(os.path.join(args.out, "results.json"), "w") as f:
        json.dump(results, f, indent=1, ensure_ascii=False)


if __name__ == "__main__":
    main()
