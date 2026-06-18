#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
SRC="$ROOT_DIR/test/localvarstruct/test_lvars.c"
OUT_DIR="${OUT_DIR:-$ROOT_DIR/test/localvarstruct/out}"

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

"$VLLVM_CLANG" "${EXTRA_ARGS[@]}" "${NO_DEBUG_ARGS[@]}" -O0 -S -emit-llvm \
  -DVLLVM_TEST_LVARS=1 "$SRC" \
  -o "$OUT_DIR/test_lvars.ll"
grep -q "vllvm.localvars" "$OUT_DIR/test_lvars.ll"
grep -q "vllvm.localvars.table.* global " "$OUT_DIR/test_lvars.ll"
grep -Eq "vllvm\\.localvars\\.table.*global \\[[0-9]+ x i32\\]" \
  "$OUT_DIR/test_lvars.ll"
if grep -q "vllvm.localvars.table.* constant " "$OUT_DIR/test_lvars.ll"; then
  echo "local variable table must be writable data, not constant data" >&2
  exit 1
fi
if grep -Eq "vllvm\\.local\\.(enc_index|index_key|field_index|offset_key\\.ptr)" \
  "$OUT_DIR/test_lvars.ll"; then
  echo "unexpected global key or FieldIndex decrypt sequence" >&2
  exit 1
fi
grep -q "load volatile" "$OUT_DIR/test_lvars.ll"
VOLATILE_LOADS=$(grep -Ec "load volatile i32" "$OUT_DIR/test_lvars.ll")
if [ "$VOLATILE_LOADS" -lt 1 ]; then
  echo "expected volatile loads for encrypted offsets" >&2
  exit 1
fi
grep -Eq "xor i32 %[0-9]+, [-0-9]+" "$OUT_DIR/test_lvars.ll"
UNIQUE_OFFSET_KEYS_IN_CODE=$(
  grep -oE "xor i32 %[0-9]+, [-0-9]+" \
    "$OUT_DIR/test_lvars.ll" |
    sed -E "s/.*xor i32 %[0-9]+, ([-0-9]+).*/\\1/" |
    sort -u |
    wc -l |
    tr -d " "
)
if [ "$UNIQUE_OFFSET_KEYS_IN_CODE" -lt 2 ]; then
  echo "expected per-slot random offset xor keys in local code" >&2
  exit 1
fi
grep -q "call.*@malloc" "$OUT_DIR/test_lvars.ll"
grep -q "call.*@free" "$OUT_DIR/test_lvars.ll"
if grep -q "alloca" "$OUT_DIR/test_lvars.ll"; then
  echo "unexpected alloca left in transformed IR" >&2
  exit 1
fi

"$VLLVM_CLANG" "${EXTRA_ARGS[@]}" "${NO_DEBUG_ARGS[@]}" -O0 "$SRC" \
  -o "$OUT_DIR/test_lvars_base"
"$VLLVM_CLANG" "${EXTRA_ARGS[@]}" "${NO_DEBUG_ARGS[@]}" -O0 \
  -DVLLVM_TEST_LVARS=1 "$SRC" \
  -o "$OUT_DIR/test_lvars"
strip_binary "$OUT_DIR/test_lvars_base"
strip_binary "$OUT_DIR/test_lvars"

set +e
"$OUT_DIR/test_lvars_base"
BASE_STATUS=$?
"$OUT_DIR/test_lvars"
LVARS_STATUS=$?
set -e

if [ "$BASE_STATUS" -ne "$LVARS_STATUS" ]; then
  echo "exit status mismatch: base=$BASE_STATUS lvars=$LVARS_STATUS" >&2
  exit 1
fi
