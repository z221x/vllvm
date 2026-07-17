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

NO_DEBUG_ARGS=(-g0)

strip_binary() {
  local bin=$1
  if command -v strip >/dev/null 2>&1; then
    strip "$bin" >/dev/null 2>&1 || strip -x "$bin" >/dev/null 2>&1 || true
  fi
}

mkdir -p "$OUT_DIR"

"$VLLVM_CLANG" "${EXTRA_ARGS[@]}" "${NO_DEBUG_ARGS[@]}" -O0 "$SRC" \
  -o "$OUT_DIR/base"
strip_binary "$OUT_DIR/base"
set +e
"$OUT_DIR/base"
BASE_STATUS=$?
set -e

run_case() {
  local name=$1
  local ll="$OUT_DIR/$name.ll"
  local exe="$OUT_DIR/$name"
  local upper_name
  upper_name=$(printf "%s" "$name" | tr "[:lower:]" "[:upper:]")
  local mode_define="-DVLLVM_TEST_${upper_name}=1"

  "$VLLVM_CLANG" "${EXTRA_ARGS[@]}" "${NO_DEBUG_ARGS[@]}" -O0 -S \
    -emit-llvm "$mode_define" "$SRC" -o "$ll"

  "$VLLVM_CLANG" "${EXTRA_ARGS[@]}" "${NO_DEBUG_ARGS[@]}" -O0 \
    "$mode_define" "$SRC" -o "$exe"
  strip_binary "$exe"

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
    grep -q "vllvm.fla.const.table" "$ll"
    grep -Eq "icmp (ult|uge) i32 %[0-9]+, [0-9]+" "$ll"
    ;;
  icall)
    grep -q "func_table" "$ll"
    grep -q "func_index_table.* global " "$ll"
    if grep -q "func_index_table.* constant " "$ll"; then
      echo "icall encrypted index table must be writable data" >&2
      exit 1
    fi
    if grep -q "@func_table.*getelementptr" "$ll"; then
      echo "icall function table must store plain function addresses" >&2
      exit 1
    fi
    grep -q "load volatile i32" "$ll"
    grep -Eq "xor i32 %[0-9]+, [-0-9]+" "$ll"
    ;;
  ibr)
    grep -q "indirectbr" "$ll"
    ;;
  fop)
    grep -q "vllvm.fop.const.table.* global " "$ll"
    grep -q "define private .*vllvm.impl.*ptr %" "$ll"
    grep -q "call .*vllvm.impl.*ptr @vllvm.fop.const.table" "$ll"
    grep -q "getelementptr i32, ptr %" "$ll"
    grep -q "func_table" "$ll"
    grep -Eq "icmp (ult|uge) i32 %[0-9]+, [0-9]+" "$ll"
    grep -q "store volatile i32 .*ptr %.*" "$ll"
    grep -Eq "xor i32 %[0-9]+, %[0-9]+" "$ll"
    if grep -Eq "@vllvm\\.bcf\\.[xy]" "$ll"; then
      echo "fop bcf constants must use the fop integer constant table" >&2
      exit 1
    fi
    if grep -Eq "load volatile i32, ptr getelementptr \\(i32, ptr @vllvm\\.fop\\.const\\.table" "$ll"; then
      echo "fop bcf table loads must use the impl table parameter" >&2
      exit 1
    fi
    if grep -Eq "vllvm\\.(localvars\\.table|fla\\.const\\.table|combined\\.const\\.table)|func_index_table" "$ll"; then
      echo "fop must use one fop integer constant table" >&2
      exit 1
    fi
    ;;
  esac
}

# run_case enstr
# run_case fla
# run_case icall
# run_case ibr
run_case fop
