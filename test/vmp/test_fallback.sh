#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
CLANG=${CLANG:-"$ROOT_DIR/build/llvm-macos/bin/clang"}
OUT_DIR=${OUT_DIR:-"$ROOT_DIR/test/vmp/out"}
SOURCE="$ROOT_DIR/test/vmp/test_fallback.c"
mkdir -p "$OUT_DIR"

EXTRA_ARGS=(-g0)
if [ "$(uname -s)" = Darwin ] && command -v xcrun >/dev/null 2>&1; then
  EXTRA_ARGS+=(-isysroot "$(xcrun --show-sdk-path)")
fi

"$CLANG" "${EXTRA_ARGS[@]}" -O0 -Rpass-missed=vmp -S -emit-llvm \
  "$SOURCE" -o "$OUT_DIR/fallback-aarch64.ll" \
  2>"$OUT_DIR/fallback-aarch64.remarks"

grep -q '"vllvm.vmp"' "$OUT_DIR/fallback-aarch64.ll"
if grep -q 'vllvm.vmp.failed' "$OUT_DIR/fallback-aarch64.ll"; then
  echo "fallback unexpectedly modified a rejected function" >&2
  exit 1
fi
grep -q '只支持可静态解析的直接调用' "$OUT_DIR/fallback-aarch64.remarks"
grep -q '暂不支持 GEP' "$OUT_DIR/fallback-aarch64.remarks"
grep -q '参数数量超过' "$OUT_DIR/fallback-aarch64.remarks"
grep -q 'HOSTCALL 参数数量超过统一 bridge 的 15 参数上限' \
  "$OUT_DIR/fallback-aarch64.remarks"
grep -q '非 volatile' "$OUT_DIR/fallback-aarch64.remarks"
if grep -q '__vllvm_vmp_code' "$OUT_DIR/fallback-aarch64.ll"; then
  echo "unsupported functions unexpectedly produced VMP code tables" >&2
  exit 1
fi

"$CLANG" --target=x86_64-unknown-linux-gnu -O0 -Rpass-missed=vmp \
  -S -emit-llvm "$SOURCE" -o "$OUT_DIR/fallback-x86.ll" \
  2>"$OUT_DIR/fallback-x86.remarks"
grep -q '仅支持 64 位小端 AArch64' "$OUT_DIR/fallback-x86.remarks"
if grep -q '__vllvm_vmp_code' "$OUT_DIR/fallback-x86.ll"; then
  echo "non-AArch64 target unexpectedly produced VMP code tables" >&2
  exit 1
fi

"$CLANG" -O2 -Rpass-missed=vmp -S -emit-llvm -x ir \
  "$ROOT_DIR/test/vmp/fastcc_fallback.ll" \
  -o "$OUT_DIR/fastcc-fallback.ll" 2>"$OUT_DIR/fastcc-fallback.remarks"
grep -q 'fastcc HOSTCALL 目标必须是当前 Module 的内部定义' \
  "$OUT_DIR/fastcc-fallback.remarks"
grep -q 'call fastcc i64 @external_fast_target' \
  "$OUT_DIR/fastcc-fallback.ll"
if grep -q '__vllvm_vmp_code' "$OUT_DIR/fastcc-fallback.ll"; then
  echo "unsafe fastcc HOSTCALL was unexpectedly virtualized" >&2
  exit 1
fi
