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
  shift
  local flags=("$@")
  local ll="$OUT_DIR/$name.ll"
  local exe="$OUT_DIR/$name"

  "$VLLVM_CLANG" "${EXTRA_ARGS[@]}" "${NO_DEBUG_ARGS[@]}" -O0 -S \
    -emit-llvm "${flags[@]}" "$SRC" -o "$ll"

  "$VLLVM_CLANG" "${EXTRA_ARGS[@]}" "${NO_DEBUG_ARGS[@]}" -O0 "${flags[@]}" \
    "$SRC" -o "$exe"
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
    grep -q "icmp slt i32" "$ll"
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
  ollvm)
    grep -q "_decrypto" "$ll"
    grep -q "func_table" "$ll"
    grep -q "indirectbr" "$ll"
    grep -q "vllvm.localvars" "$ll"
    grep -q "vllvm.localvars.table.* global " "$ll"
    grep -Eq "vllvm\\.localvars\\.table.*global \\[[0-9]+ x i[0-9]+\\]" "$ll"
    if grep -q "vllvm.localvars.table.* constant " "$ll"; then
      echo "ollvm local variable table must be writable data" >&2
      exit 1
    fi
    if grep -Eq "vllvm\\.local\\.(enc_index|index_key|field_index|offset_key\\.ptr)" "$ll"; then
      echo "ollvm local variable table must keep keys out of global data" >&2
      exit 1
    fi
    grep -q "load volatile" "$ll"
    local volatile_loads
    volatile_loads=$(grep -Ec "load volatile i[0-9]+" "$ll")
    if [ "$volatile_loads" -lt 1 ]; then
      echo "ollvm local variable table must load encrypted offsets" >&2
      exit 1
    fi
    grep -Eq "xor i[0-9]+ %[0-9]+, [-0-9]+" "$ll"
    local unique_offset_keys_in_code
    unique_offset_keys_in_code=$(
      grep -oE "xor i[0-9]+ %[0-9]+, [-0-9]+" "$ll" |
        sed -E "s/.*xor i[0-9]+ %[0-9]+, ([-0-9]+).*/\\1/" |
        sort -u |
        wc -l |
        tr -d " "
    )
    if [ "$unique_offset_keys_in_code" -lt 2 ]; then
      echo "ollvm local variable table must use per-slot local offset keys" >&2
      exit 1
    fi
    ;;
  esac
}

run_case enstr -enstr
run_case fla -fla
run_case icall -icall
run_case ibr -ibr
run_case ollvm -ollvm
