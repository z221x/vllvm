#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
SRC="$ROOT_DIR/test/indirectcall/test_indirectcall.c"
OUT_DIR="${OUT_DIR:-$ROOT_DIR/test/indirectcall/out}"

if [ -n "${CLANG:-}" ]; then
  VLLVM_CLANG="$CLANG"
elif [ -x "$ROOT_DIR/build/llvm-macos/bin/clang" ]; then
  VLLVM_CLANG="$ROOT_DIR/build/llvm-macos/bin/clang"
elif [ -x "$ROOT_DIR/build/llvm-linux/bin/clang" ]; then
  VLLVM_CLANG="$ROOT_DIR/build/llvm-linux/bin/clang"
else
  VLLVM_CLANG=clang
fi

mkdir -p "$OUT_DIR"

TARGET_ARGS=(-target aarch64-unknown-linux-gnu -Xclang -llvm-verify-each)
LINK_ARGS=(-Xclang -llvm-verify-each)
if [ "$(uname -s)" = Darwin ]; then
  SDK_PATH=$(xcrun --show-sdk-path)
  TARGET_ARGS=(-target arm64-apple-macos -isysroot "$SDK_PATH" -Xclang -llvm-verify-each)
  LINK_ARGS=(-isysroot "$SDK_PATH" -Xclang -llvm-verify-each)
fi

"$VLLVM_CLANG" "${TARGET_ARGS[@]}" -O0 -S -emit-llvm \
  -DVLLVM_TEST_ICALL=1 "$SRC" -o "$OUT_DIR/icall.ll"
"$VLLVM_CLANG" "${TARGET_ARGS[@]}" -O0 -S \
  -DVLLVM_TEST_ICALL=1 "$SRC" -o "$OUT_DIR/icall.s"

grep -q "call icallcc" "$OUT_DIR/icall.ll"
grep -Eq "i32 nest [0-9]+" "$OUT_DIR/icall.ll"
if grep -q "vllvm.icall.index_array" "$OUT_DIR/icall.ll"; then
  echo "icall packed indexes must be call-site constants" >&2
  exit 1
fi
grep -q "register_func" "$OUT_DIR/icall.ll"
grep -q "create_func_pool" "$OUT_DIR/icall.ll"
grep -Eq "i32 0, ptr @__vllvm_icall\..*\.register_funcs" "$OUT_DIR/icall.ll"
grep -Eq "ubfx[[:space:]]+w16, w19, #16, #8" "$OUT_DIR/icall.s"
grep -Eq "mov[[:space:]]+w19," "$OUT_DIR/icall.s"

# 全模块只有 add_bias、mix_double、sum_nine 三个去重后的注册目标。
REGISTER_CALLS=$(grep -Ec "call void @.*register_func" "$OUT_DIR/icall.ll")
if [ "$REGISTER_CALLS" -ne 3 ]; then
  echo "expected 3 deduplicated register_func calls, got $REGISTER_CALLS" >&2
  exit 1
fi

if [ "$(uname -s)" = Darwin ]; then
  "$VLLVM_CLANG" -target aarch64-unknown-linux-gnu -O0 -ffreestanding \
    -DVLLVM_TEST_ICALL=1 -c "$SRC" -o "$OUT_DIR/icall-linux.o"
fi

case "$($VLLVM_CLANG -dumpmachine)" in
  arm64-*|aarch64-*)
    "$VLLVM_CLANG" "${LINK_ARGS[@]}" -O0 "$SRC" -o "$OUT_DIR/base"
    "$VLLVM_CLANG" "${LINK_ARGS[@]}" -O0 -DVLLVM_TEST_ICALL=1 \
      "$SRC" -o "$OUT_DIR/icall"
    "$OUT_DIR/base"
    "$OUT_DIR/icall"
    ;;
esac
