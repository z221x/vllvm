#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
SRC="$ROOT_DIR/test/boguscontrolflow/test_bcf.c"
OUT_DIR="${OUT_DIR:-$ROOT_DIR/test/boguscontrolflow/out}"

if [ -n "${CLANG:-}" ]; then
  VLLVM_CLANG="$CLANG"
elif [ -x "$ROOT_DIR/build/llvm-macos/bin/clang" ]; then
  VLLVM_CLANG="$ROOT_DIR/build/llvm-macos/bin/clang"
elif [ -x "$ROOT_DIR/build/llvm-linux/bin/clang" ]; then
  VLLVM_CLANG="$ROOT_DIR/build/llvm-linux/bin/clang"
else
  VLLVM_CLANG="clang"
fi

EXTRA_ARGS=()
if [ "$(uname -s)" = "Darwin" ] && command -v xcrun >/dev/null 2>&1; then
  SDK_PATH=$(xcrun --show-sdk-path 2>/dev/null || true)
  if [ -n "$SDK_PATH" ]; then
    EXTRA_ARGS+=(-isysroot "$SDK_PATH")
  fi
fi

mkdir -p "$OUT_DIR"

"$VLLVM_CLANG" "${EXTRA_ARGS[@]}" -g0 -O0 "$SRC" -o "$OUT_DIR/base"
set +e
"$OUT_DIR/base"
BASE_STATUS=$?
set -e

"$VLLVM_CLANG" "${EXTRA_ARGS[@]}" -g0 -O0 -S -emit-llvm \
  -DVLLVM_TEST_BCF=1 "$SRC" -o "$OUT_DIR/bcf.ll"

"$VLLVM_CLANG" "${EXTRA_ARGS[@]}" -g0 -O0 \
  -DVLLVM_TEST_BCF=1 "$SRC" -o "$OUT_DIR/bcf"
set +e
"$OUT_DIR/bcf"
BCF_STATUS=$?
set -e

if [ "$BCF_STATUS" -ne "$BASE_STATUS" ]; then
  echo "bcf exit status mismatch: base=$BASE_STATUS pass=$BCF_STATUS" >&2
  exit 1
fi

grep -q "@vllvm.bcf.x" "$OUT_DIR/bcf.ll"
grep -q "@vllvm.bcf.y" "$OUT_DIR/bcf.ll"
grep -q "load volatile i32, ptr @vllvm.bcf" "$OUT_DIR/bcf.ll"
grep -q "store volatile i32 .*@vllvm.bcf.x" "$OUT_DIR/bcf.ll"
