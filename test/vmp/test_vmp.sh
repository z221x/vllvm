#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
SRC="$ROOT_DIR/test/vmp/test_vmp.c"
OUT_DIR=${OUT_DIR:-"$ROOT_DIR/test/vmp/out"}

if [ -n "${CLANG:-}" ]; then
  VLLVM_CLANG=$CLANG
elif [ -x "$ROOT_DIR/build/llvm-macos/bin/clang" ]; then
  VLLVM_CLANG="$ROOT_DIR/build/llvm-macos/bin/clang"
else
  VLLVM_CLANG="$ROOT_DIR/build/llvm-linux/bin/clang"
fi

EXTRA_ARGS=(-Xclang -llvm-verify-each)
if [ "$(uname -s)" = Darwin ] && command -v xcrun >/dev/null 2>&1; then
  SDK_PATH=$(xcrun --show-sdk-path)
  EXTRA_ARGS+=(-isysroot "$SDK_PATH")
fi

mkdir -p "$OUT_DIR"
for opt in 0 2; do
  "$VLLVM_CLANG" "${EXTRA_ARGS[@]}" -g0 -O"$opt" "$SRC" \
    -pthread -o "$OUT_DIR/native_O$opt"
  "$VLLVM_CLANG" "${EXTRA_ARGS[@]}" -g0 -O"$opt" -DVLLVM_TEST_VMP=1 \
    "$SRC" -pthread -o "$OUT_DIR/vmp_O$opt"
  "$VLLVM_CLANG" "${EXTRA_ARGS[@]}" -g0 -O"$opt" -DVLLVM_TEST_VMP=1 \
    -S -emit-llvm "$SRC" -o "$OUT_DIR/vmp_O$opt.ll"

  native_output=$("$OUT_DIR/native_O$opt")
  vmp_output=$("$OUT_DIR/vmp_O$opt")
  if [ "$native_output" != "$vmp_output" ]; then
    echo "O$opt native/VMP output mismatch" >&2
    echo "native: $native_output" >&2
    echo "vmp:    $vmp_output" >&2
    exit 1
  fi
  grep -q '__vllvm_vmp_code' "$OUT_DIR/vmp_O$opt.ll"
  grep -q '__vllvm_vmp_values' "$OUT_DIR/vmp_O$opt.ll"
  grep -q '__vllvm_vmp_functions\.protected_hostcall' "$OUT_DIR/vmp_O$opt.ll"
  grep -q '__vllvm_vmp_functions\.protected_hostcall.*\[4 x ptr\].*host_call_eight.*host_call_nine.*host_call_ten_narrow.*host_call_fifteen' \
    "$OUT_DIR/vmp_O$opt.ll"
  grep -q 'define linkonce_odr hidden i64 @__vllvm_vmp_hostcall_bridge' \
    "$OUT_DIR/vmp_O$opt.ll"
  if grep -q '__vllvm_vmp_bridge\.' "$OUT_DIR/vmp_O$opt.ll"; then
    echo "O$opt unexpectedly contains a per-function HOSTCALL bridge" >&2
    exit 1
  fi
  if grep -q 'define internal fastcc i64 @host_call_' "$OUT_DIR/vmp_O$opt.ll"; then
    echo "O$opt did not normalize an internal fastcc HOSTCALL target" >&2
    exit 1
  fi
  grep -q 'call i64 @__vllvm_vmp_execute' "$OUT_DIR/vmp_O$opt.ll"
  grep -q 'define.*i64 @__vllvm_vmp_execute(ptr.*i32.*ptr.*ptr.*i32.*ptr.*i32.*ptr.*i32' \
    "$OUT_DIR/vmp_O$opt.ll"
  grep -q 'vllvm.vmp.processed' "$OUT_DIR/vmp_O$opt.ll"
  grep -q '@llvm.compiler.used' "$OUT_DIR/vmp_O$opt.ll"
  if grep -q '__vllvm_vmp_image' "$OUT_DIR/vmp_O$opt.ll"; then
    echo "O$opt unexpectedly contains the removed VMP image Blob" >&2
    exit 1
  fi
  if grep -q 'vmp-enstr-marker' "$OUT_DIR/vmp_O$opt.ll"; then
    echo "O$opt VMP+enstr left a plaintext string" >&2
    exit 1
  fi
  if grep -Eq 'vllvm\.(vmfla|localvars|fla)\.|indirectbr' \
      "$OUT_DIR/vmp_O$opt.ll"; then
    echo "O$opt VMP candidate unexpectedly ran a conflicting function pass" >&2
    exit 1
  fi
  if [ "$(grep -c 'define.*@__vllvm_vmp_execute' "$OUT_DIR/vmp_O$opt.ll")" \
       -ne 1 ]; then
    echo "O$opt contains more than one embedded VMP runtime" >&2
    exit 1
  fi
done

# processed 标记必须让完整 LTO 管线保持幂等，且 linkonce_odr 运行时只能
# 合并为一份可链接实现。
"$VLLVM_CLANG" "${EXTRA_ARGS[@]}" -g0 -O2 -flto "$SRC" -pthread \
  -o "$OUT_DIR/native_lto"
"$VLLVM_CLANG" "${EXTRA_ARGS[@]}" -g0 -O2 -flto -DVLLVM_TEST_VMP=1 \
  "$SRC" -pthread -o "$OUT_DIR/vmp_lto"
native_lto_output=$("$OUT_DIR/native_lto")
vmp_lto_output=$("$OUT_DIR/vmp_lto")
if [ "$native_lto_output" != "$vmp_lto_output" ]; then
  echo "LTO native/VMP output mismatch" >&2
  exit 1
fi
