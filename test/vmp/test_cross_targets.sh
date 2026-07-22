#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
CLANG=${CLANG:-"$ROOT_DIR/build/llvm-macos/bin/clang"}
OUT_DIR=${OUT_DIR:-"$ROOT_DIR/test/vmp/out"}
SOURCE="$ROOT_DIR/test/vmp/cross_target.ll"
mkdir -p "$OUT_DIR"

for target in aarch64-unknown-linux-gnu aarch64-pc-windows-msvc; do
  output="$OUT_DIR/cross-${target}.ll"
  "$CLANG" --target="$target" -O2 -S -emit-llvm -x ir "$SOURCE" \
    -o "$output"
  grep -q '__vllvm_vmp_code' "$output"
  grep -q '__vllvm_vmp_functions.cross_target_call' "$output"
  grep -q '__vllvm_vmp_hostcall_bridge' "$output"
  grep -q '__vllvm_vmp_functions.cross_target_call.*cross_host_eight' "$output"
  if grep -q '__vllvm_vmp_bridge\.' "$output" ||
     grep -q 'define internal fastcc i64 @cross_host_eight' "$output"; then
    echo "$target retained a per-target bridge or fastcc HOSTCALL target" >&2
    exit 1
  fi
  grep -q 'define.*@__vllvm_vmp_execute' "$output"
  grep -q 'vllvm.vmp.processed' "$output"
  "$CLANG" --target="$target" -O2 -c -x ir "$output" \
    -o "$OUT_DIR/cross-${target}.o"
done
