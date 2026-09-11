#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
SRC="$ROOT_DIR/test/merge/test_merge.c"
OUT_DIR="${OUT_DIR:-$ROOT_DIR/test/merge/out}"

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
BASE_OUT=$("$OUT_DIR/base")
echo "$BASE_OUT"

"$VLLVM_CLANG" "${EXTRA_ARGS[@]}" -g0 -O0 -S -emit-llvm "$SRC" \
  -o "$OUT_DIR/test_merge.ll"
"$VLLVM_CLANG" "${EXTRA_ARGS[@]}" -g0 -O0 "$SRC" -o "$OUT_DIR/test_merge"
OBF_OUT=$("$OUT_DIR/test_merge")
echo "$OBF_OUT"

if [ "$BASE_OUT" != "$OBF_OUT" ]; then
  echo "merge runtime output mismatch: base='$BASE_OUT' obf='$OBF_OUT'" >&2
  exit 1
fi

LL="$OUT_DIR/test_merge.ll"

# 断言：生成 internal dispatcher，命名带 vllvm.merge 前缀。
grep -Eq 'define internal[^@]*@vllvm\.merge\.' "$LL"

# 断言：main 里的调用点被改写为携带 key 常量的 dispatcher 调用。
grep -Eq 'call[^@]*@vllvm\.merge\.[a-z]+' "$LL"

# 断言：成员体内联进 dispatcher 后，静态成员函数已删除。
if grep -Eq 'define[^@]*@merge_[ab]\(' "$LL"; then
  echo "merge members should be inlined and erased" >&2
  exit 1
fi

echo "test_merge passed"
