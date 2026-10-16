#!/usr/bin/env python3
"""CPython 应用级基准：configure + CC 记录器做一次基线构建，捕获全部
编译/归档/链接命令；混淆配置按捕获的命令表重放（.c 走两段式注入，
链接/归档把 .o 换成镜像目录里的混淆产物）。用法：

  python3 test/perf/run_python_bench.py [--configs base,vmfla,bcfvmfla] \
      [--runs 6] [--out tmp/perfbench/python]

源码树缓存在 test/perf/apps（或 tmp/perfbench/apps）/python-3.12.8，
命令日志缓存在 <out>/cmdlog（基线只构建一次）。
"""
import argparse
import json
import os
import shlex
import shutil
import statistics
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, HERE)

from run_perfbench import APPS_DIR, CONFIGS, WARMUP, TIMED_RUNS, find_clang

PY_VER = "3.12.8"
PY_URL = "https://www.python.org/ftp/python/%s/Python-%s.tgz" % (PY_VER,
                                                                 PY_VER)


def sh(cwd, cmd, timeout=None, log=None):
    p = subprocess.run(cmd, cwd=cwd, capture_output=True, text=True,
                       timeout=timeout, shell=isinstance(cmd, str))
    if log and p.returncode != 0:
        log("FAILED %s: %s" % (cmd if isinstance(cmd, str) else cmd[:3],
                               p.stderr[-800:]))
    return p


def prepare_src(apps_root, clang, sdk, log):
    """下载/解压/configure（CC 经环境变量传入）。"""
    pydir = os.path.join(apps_root, "python-%s" % PY_VER)
    if not os.path.isdir(pydir):
        tgz = os.path.join(apps_root, "python-%s.tgz" % PY_VER)
        if not os.path.isfile(tgz):
            log("downloading %s" % PY_URL)
            r = subprocess.run(["curl", "-sSL", "-o", tgz, PY_URL],
                               timeout=600)
            if r.returncode != 0:
                sys.exit("download python failed")
        sh(apps_root, ["tar", "xzf", tgz])
    if not os.path.isfile(os.path.join(pydir, "Makefile")):
        env = dict(os.environ)
        env["CC"] = clang
        env["CFLAGS"] = "-O0 -g0" + ("".join(" " + a for a in sdk))
        # macOS SDK 的 LibreSSL 头编不过 _ssl；负载（json/数值/字符串）
        # 用不到 ssl，直接禁用相关模块。
        env["py_cv_module__ssl"] = "disabled"
        env["py_cv_module__hashlib"] = "disabled"
        env["py_cv_module__ssl_sni"] = "n/a"
        # Homebrew openssl@4 头与 py3.12 _ssl 不兼容；指向不存在的目录
        # 让探测失败，_ssl/_hashlib 标记 missing（基准负载不需要它们）。
        p = subprocess.run(["./configure", "--with-ensurepip=no",
                            "--disable-test-modules",
                            "--with-openssl=/nonexistent"],
                           cwd=pydir, env=env,
                           capture_output=True, text=True, timeout=900)
        if p.returncode != 0:
            sys.exit("configure failed: %s" % p.stderr[-1500:])
    return pydir


def baseline_build(pydir, clang, sdk, cmdlog, jobs, log):
    """带记录器的一次构建：wrapper 记录全部 CC 调用后转发真编译器。
    日志先写 .part，构建成功才落成完整 cmdlog（防半截缓存）。"""
    if os.path.isfile(cmdlog):
        return
    part = cmdlog + ".part"
    if os.path.isfile(part):
        os.remove(part)
    wrapper = os.path.join(os.path.dirname(cmdlog), "ccwrap.sh")
    with open(wrapper, "w") as f:
        # 逐段拼接：模板不能整体走 Python % 格式化，否则 shell
        # printf 自身的 %s 转换会被吞掉（日志只剩空行）。
        f.write('#!/bin/sh\n'
                'printf "\\002%s\\n" "$*" >> ' + part + '\n'
                'exec ' + clang + ' "$@"\n')
    os.chmod(wrapper, 0o755)
    p = sh(pydir, ["make", "-j%d" % jobs, "CC=%s" % wrapper,
                   "CFLAGS=-O0 -g0", "LINKCC=%s" % wrapper],
           timeout=7200, log=log)
    if p.returncode != 0:
        sys.exit("python baseline build failed: %s" % p.stderr[-1500:])
    os.rename(part, cmdlog)


def parse_cmdlog(cmdlog):
    """拆出编译命令（.c -> .o）、ar、链接命令。"""
    compiles, ars, links = [], [], []
    def unq(tok):
        # posix=False 会保留引号；-D/-U 的值需要内层引号（如
        # -DPLATFORM="darwin" 的字符串字面量），其余记号去掉外引号。
        if len(tok) >= 2 and tok[0] == tok[-1] and tok[0] in "\"'":
            return tok[1:-1]
        return tok

    for line in open(cmdlog):
        # 只认 wrapper 打头的完整调用行；无头标记的是跨行参数碎片。
        if not line.startswith("\002"):
            continue
        line = line[1:].strip()
        if not line:
            continue
        try:
            argv = [t if t.startswith(("-D", "-U")) else unq(t)
                    for t in shlex.split(line, posix=False)]
        except ValueError:
            continue
        if not argv:
            continue
        has_c = any(a.endswith(".c") for a in argv)
        out_o = None
        if "-c" in argv and has_c:
            try:
                out_o = argv[argv.index("-o") + 1]
            except (ValueError, IndexError):
                continue
            compiles.append((argv, out_o))
        elif argv[0].endswith("ar"):
            ars.append(argv)
        elif "-o" in argv and any(a.endswith(".o") for a in argv):
            try:
                links.append((argv, argv[argv.index("-o") + 1]))
            except (ValueError, IndexError):
                continue
    return compiles, ars, links


def mirror_obj(workdir, out_o):
    rel = out_o.lstrip("./")
    return os.path.join(workdir, "objs", rel)


def build_config(pydir, workdir, config, attrs, cmdlog, clang, sdk, jobs,
                 log, tu_timeout):
    """按命令表重放：.c 两段式注入编译，ar/link 换镜像 .o 后重放。"""
    compiles, ars, links = parse_cmdlog(cmdlog)
    os.makedirs(os.path.join(workdir, "objs"), exist_ok=True)
    inject = os.path.join(HERE, "inject_vllvm_attrs.py")
    objs_dir = os.path.join(workdir, "objs")

    todo = []
    for argv, out_o in compiles:
        dst = mirror_obj(workdir, out_o)
        os.makedirs(os.path.dirname(dst), exist_ok=True)
        src_c = None
        for a in argv:
            if a.endswith(".c"):
                src_c = a
        # 阶段一：从捕获命令推出 -I/-D/-U/-include/-std（python 的
        # 头文件路径多在 -c 之后，需全 argv 扫描；其余 -f/-W/O 旗标
        # 对 IR 生成无意义，丢弃）。
        keep = []
        skip = False
        for a in argv:
            if skip:
                # 当前 token 是上一个两参旗标的值，原样保留。
                skip = False
                keep.append(a)
                continue
            # 两参旗标（-I X 等）按独立列表项保留，不能拼成带空格的
            # 单参数（-isysroot 会带着前导空格变成非法 sysroot）。
            if a in ("-I", "-D", "-U", "-include", "-std", "-isysroot"):
                keep.append(a)
                skip = True
                continue
            if a.startswith(("-I", "-D", "-U", "-include", "-std=")):
                keep.append(a)
            elif a in ("-c", "-o") or a.endswith((".c", ".o")):
                continue
        ll = dst + ".ll"
        p = subprocess.run([clang] + sdk + ["-O0", "-g0", "-S", "-emit-llvm",
                                            "-Xclang", "-disable-O0-optnone"]
                           + keep + ["-c", src_c, "-o", ll],
                           cwd=pydir, capture_output=True, text=True,
                           timeout=tu_timeout)
        if p.returncode != 0:
            sys.exit("py stage1 failed %s: %s" % (src_c, p.stderr[-1200:]))
        # 阶段二：注入属性后编译
        if attrs:
            r = subprocess.run([sys.executable, inject, ll, ll,
                                "--attrs", ",".join(attrs)],
                               capture_output=True, text=True, timeout=120)
            if r.returncode != 0:
                sys.exit("py inject failed %s: %s" % (src_c, r.stderr))
        r = subprocess.run([clang] + sdk + ["-O0", "-g0", "-c", ll, "-o", dst],
                           cwd=pydir, capture_output=True, text=True,
                           timeout=tu_timeout)
        if r.returncode != 0:
            sys.exit("py stage2 failed %s: %s" % (src_c, r.stderr[-1200:]))
        todo.append(dst)

    # 链接重放。注意两点：
    # 1. 日志只记录参数（$* 不含编译器路径），链接命令需补回 clang。
    # 2. 静态库 libpython*.a 由 make 直接调 ar 归档（不经过 CC wrapper，
    #    不在日志里），其中的基线对象会稀释混淆产物——按 `ar t` 的成员
    #    清单从镜像对象重建同名档案。
    def remap(a):
        if a.endswith(".o") and os.path.isfile(mirror_obj(workdir, a)):
            return mirror_obj(workdir, a)
        return a

    for argv, out_name in links:
        if any(a.endswith(".a") for a in argv):
            for a in list(argv):
                if not a.endswith(".a") or a.startswith("-"):
                    continue
                members = subprocess.run(["ar", "t", a], cwd=pydir,
                                         capture_output=True, text=True)
                if members.returncode != 0:
                    continue
                pick = []
                for m in members.stdout.split():
                    cand = mirror_obj(workdir, m)
                    if os.path.isfile(cand):
                        pick.append(cand)
                if not pick:
                    continue
                dst_a = os.path.join(objs_dir, os.path.basename(a))
                os.makedirs(os.path.dirname(dst_a), exist_ok=True)
                if os.path.isfile(dst_a):
                    os.remove(dst_a)
                r = subprocess.run(["ar", "rcs", dst_a] + pick, cwd=pydir,
                                   capture_output=True, text=True,
                                   timeout=600)
                if r.returncode != 0:
                    sys.exit("py re-ar failed %s: %s" % (a, r.stderr[-600:]))

                def swap(x, _a=a, _d=dst_a):
                    return _d if x == _a else x
                argv = [swap(x) for x in argv]

        new_argv = [clang] + [remap(a) for a in argv]
        dst = os.path.join(objs_dir, out_name.lstrip("./"))
        os.makedirs(os.path.dirname(dst), exist_ok=True)
        try:
            oi = new_argv.index("-o")
            new_argv[oi + 1] = dst
        except ValueError:
            new_argv += ["-o", dst]
        p = subprocess.run(new_argv, cwd=pydir, capture_output=True,
                           text=True, timeout=1800)
        if p.returncode != 0:
            continue
        if out_name.endswith("python.exe") or out_name == "python" or \
           out_name.endswith("/python"):
            exe = dst
    if exe is None:
        sys.exit("py link: no python executable produced")
    subprocess.run(["strip", exe], capture_output=True)
    return exe


def run_workload(exe, pydir, runs, log, run_timeout):
    script = os.path.join(HERE, "workloads", "bench_python.py")
    outs, times = [], []
    # stdlib 探测按可执行文件位置进行：把混淆版 exe 放回源码树内
    # （与基线同目录布局），各配置用同一份 Lib/。
    if os.path.dirname(os.path.abspath(exe)) != os.path.abspath(pydir):
        staged = os.path.join(pydir, "python_bench_obf")
        shutil.copyfile(exe, staged)
        os.chmod(staged, 0o755)
        exe = staged
    p0 = subprocess.run([exe, script], capture_output=True, text=True,
                        timeout=run_timeout)
    if p0.returncode != 0:
        sys.exit("python workload failed: %s" % p0.stderr[-800:])
    ref = p0.stdout
    for _ in range(runs):
        t0 = time.perf_counter()
        p = subprocess.run([exe, script], capture_output=True, text=True,
                           timeout=run_timeout)
        times.append(time.perf_counter() - t0)
        if p.returncode != 0 or p.stdout != ref:
            sys.exit("python output mismatch")
    return ref, times


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--configs", default="base,vmfla,bcfvmfla")
    ap.add_argument("--runs", type=int, default=WARMUP + TIMED_RUNS)
    ap.add_argument("--jobs", type=int, default=6)
    ap.add_argument("--tu-timeout", type=float, default=1800)
    ap.add_argument("--run-timeout", type=float, default=1800)
    ap.add_argument("--out", default=None)
    args = ap.parse_args()

    clang = find_clang()
    sdk = []
    import sys as _s
    if _s.platform == "darwin":
        p = subprocess.run(["xcrun", "--show-sdk-path"],
                           capture_output=True, text=True)
        if p.returncode == 0 and p.stdout.strip():
            sdk = ["-isysroot", p.stdout.strip()]

    apps_root = APPS_DIR if os.path.isdir(APPS_DIR) else os.path.join(
        ROOT, "tmp", "perfbench", "apps")
    outdir = os.path.abspath(args.out or os.path.join(
        ROOT, "tmp/perfbench/python"))
    os.makedirs(outdir, exist_ok=True)
    logf = open(os.path.join(outdir, "bench.log"), "a")

    def log(msg):
        line = "[%s] %s" % (time.strftime("%H:%M:%S"), msg)
        print(line, flush=True)
        logf.write(line + "\n")
        logf.flush()

    pydir = prepare_src(apps_root, clang, sdk, log)
    cmdlog = os.path.join(outdir, "cmdlog")
    baseline_build(pydir, clang, sdk, cmdlog, args.jobs, log)
    log("baseline ready; cmdlog=%s" % cmdlog)

    configs = [c for c in args.configs.split(",") if c]
    for c in configs:
        if c not in CONFIGS:
            sys.exit("unknown config: %s" % c)

    results = {}
    ref_out = None
    for config in configs:
        workdir = os.path.join(outdir, config)
        os.makedirs(workdir, exist_ok=True)
        if config == "base":
            exe = os.path.join(pydir, "python.exe")
            if not os.path.isfile(exe):
                exe = os.path.join(pydir, "python")
        else:
            t0 = time.time()
            exe = build_config(pydir, workdir, config, CONFIGS[config],
                               cmdlog, clang, sdk, args.jobs, log,
                               args.tu_timeout)
            log("build python/%s: %.0fs %s" % (config, time.time() - t0, exe))
        out, times = run_workload(exe, pydir, args.runs, log,
                                  args.run_timeout)
        if ref_out is None:
            ref_out = out
        elif out != ref_out:
            sys.exit("python output mismatch for config %s" % config)
        results[config] = {"median_s": round(statistics.median(times), 4),
                           "runs": [round(t, 3) for t in times],
                           "size_bytes": os.path.getsize(exe)}
        log("run python/%s: median %.3fs" % (
            config, statistics.median(times)))

    base = results["base"]["median_s"]
    print("\n%-10s %10s %8s" % ("config", "median", "vs base"))
    for config in configs:
        print("%-10s %9.3fs %7.2fx" % (
            config, results[config]["median_s"],
            results[config]["median_s"] / base))
    with open(os.path.join(outdir, "results.json"), "w") as f:
        json.dump(results, f, indent=1, ensure_ascii=False)
    log("results -> %s" % os.path.join(outdir, "results.json"))


if __name__ == "__main__":
    main()
