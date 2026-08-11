#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
SRC="$ROOT_DIR/test/indirectbranch/test_indirectbr.c"
OUT_DIR="${OUT_DIR:-$ROOT_DIR/test/indirectbranch/out}"

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

"$VLLVM_CLANG" "${EXTRA_ARGS[@]}" -g0 -O0 "$SRC" \
  -o "$OUT_DIR/test_indirectbr_base"
"$VLLVM_CLANG" "${EXTRA_ARGS[@]}" -g0 -O0 -DVLLVM_TEST_IBR=1 "$SRC" \
  -o "$OUT_DIR/test_indirectbr"
"$VLLVM_CLANG" "${EXTRA_ARGS[@]}" -g0 -O0 -DVLLVM_TEST_IBR=1 \
  -S -emit-llvm "$SRC" -o "$OUT_DIR/test_indirectbr.ll"

base_output=$("$OUT_DIR/test_indirectbr_base")
ibr_output=$("$OUT_DIR/test_indirectbr")
if [ "$base_output" != "$ibr_output" ]; then
  echo "indirect branch output mismatch" >&2
  echo "base: $base_output" >&2
  echo "ibr:  $ibr_output" >&2
  exit 1
fi

grep -q "indirectbr" "$OUT_DIR/test_indirectbr.ll"
if ! grep -q "^  switch " "$OUT_DIR/test_indirectbr.ll"; then
  echo "IndirectBranchPass must not rewrite switch terminators" >&2
  exit 1
fi
