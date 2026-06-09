#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
SRC="$ROOT_DIR/test/complex/test_passes_complex.c"
OUT_DIR="${OUT_DIR:-$ROOT_DIR/test/complex/out}"

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

"$VLLVM_CLANG" "${EXTRA_ARGS[@]}" -O0 "$SRC" -o "$OUT_DIR/base"
set +e
"$OUT_DIR/base"
BASE_STATUS=$?
set -e

run_case() {
  local name=$1
  shift
  local flags=("$@")
  local ll="$OUT_DIR/$name.ll"
  local exe="$OUT_DIR/$name"

  "$VLLVM_CLANG" "${EXTRA_ARGS[@]}" -O0 -S -emit-llvm "${flags[@]}" "$SRC" \
    -o "$ll"
  "$VLLVM_CLANG" "${EXTRA_ARGS[@]}" -O0 "${flags[@]}" "$SRC" -o "$exe"

  set +e
  "$exe"
  local status=$?
  set -e

  if [ "$status" -ne "$BASE_STATUS" ]; then
    echo "$name exit status mismatch: base=$BASE_STATUS pass=$status" >&2
    exit 1
  fi

  case "$name" in
  enstr)
    grep -q "_decrypto" "$ll"
    if grep -q "alpha:local-string-pass" "$ll"; then
      echo "enstr left plaintext alpha string in IR" >&2
      exit 1
    fi
    ;;
  fla)
    if grep -q "^  switch " "$ll"; then
      echo "fla left a switch instruction in IR" >&2
      exit 1
    fi
    grep -q "icmp slt i32" "$ll"
    ;;
  icall)
    grep -q "func_table" "$ll"
    ;;
  ibr)
    grep -q "indirectbr" "$ll"
    ;;
  ollvm)
    grep -q "_decrypto" "$ll"
    grep -q "func_table" "$ll"
    grep -q "indirectbr" "$ll"
    grep -q "vllvm.localvars" "$ll"
    ;;
  esac
}

run_case enstr -enstr
run_case fla -fla
run_case icall -icall
run_case ibr -ibr
run_case ollvm -ollvm
