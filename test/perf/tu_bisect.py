#!/usr/bin/env python3
"""对 stage1 IR 做单 TU 标注二分，定位哪个编译单元引入故障。"""
import os
import shutil
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
CLANG = os.path.join(os.path.dirname(os.path.dirname(HERE)),
                     "build/llvm-macos/bin/clang")
SDK = subprocess.run(["xcrun", "--show-sdk-path"], capture_output=True,
                     text=True, check=True).stdout.strip()


def build(stage1_dir, workdir, attrs, only_tu):
    os.makedirs(workdir, exist_ok=True)
    objs = []
    for f in sorted(os.listdir(stage1_dir)):
        if not f.endswith(".ll"):
            continue
        tu = f[:-3]
        ll = os.path.join(workdir, f)
        if tu == only_tu:
            subprocess.run([sys.executable,
                            os.path.join(HERE, "inject_vllvm_attrs.py"),
                            os.path.join(stage1_dir, f), ll,
                            "--attrs", attrs],
                           check=True, capture_output=True)
        else:
            shutil.copyfile(os.path.join(stage1, f) if False else
                            os.path.join(stage1_dir, f), ll)
        obj = os.path.join(workdir, tu + ".o")
        r = subprocess.run([CLANG, "-isysroot", SDK, "-O0", "-g0", "-c", ll,
                            "-o", obj], capture_output=True, text=True)
        if r.returncode != 0:
            return None, tu, r.stderr[-500:]
        objs.append(obj)
    exe = os.path.join(workdir, "app.bin")
    r = subprocess.run([CLANG, "-isysroot", SDK, "-fuse-ld=/usr/bin/ld",
                        "-O2", "-g0"] + objs + ["-lm", "-o", exe],
                       capture_output=True, text=True)
    if r.returncode != 0:
        return None, tu, r.stderr[-500:]
    return exe, tu, ""


def main():
    stage1 = sys.argv[1]       # stage1 目录
    attrs = sys.argv[2]        # 如 vllvm.vmfla
    test_cmd = sys.argv[3:]    # exe 占位 {exe}
    ok, bad = [], []
    for f in sorted(os.listdir(stage1)):
        if not f.endswith(".ll"):
            continue
        tu = f[:-3]
        workdir = os.path.join(stage1.rstrip("/"), "..", "bisect_" + tu)
        exe, _, err = build(stage1, workdir, attrs, tu)
        if exe is None:
            bad.append((tu, "build: " + err))
            continue
        cmd = [c.replace("{exe}", exe) for c in test_cmd]
        try:
            r = subprocess.run(cmd, capture_output=True, text=True,
                               timeout=300)
            status = "OK" if r.returncode == 0 else "rc=%d" % r.returncode
        except subprocess.TimeoutExpired:
            status = "TIMEOUT"
        if status == "OK":
            ok.append(tu)
        else:
            bad.append((tu, status))
        print("%-14s %s" % (tu, status), flush=True)
    print("--- failing TUs:", [b[0] for b in bad])


if __name__ == "__main__":
    main()
