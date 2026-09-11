#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
SRC="$ROOT_DIR/test/bb2func/test_bb2func.c"
OUT_DIR="${OUT_DIR:-$ROOT_DIR/test/bb2func/out}"

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

# 基线：不混淆直接编译运行。
"$VLLVM_CLANG" "${EXTRA_ARGS[@]}" -g0 -O0 "$SRC" -o "$OUT_DIR/base"
BASE_OUT=$("$OUT_DIR/base")
echo "$BASE_OUT"

# 混淆 IR：-emit-llvm 同时会跑 vllvm 管线，IR 里应出现 bb2func 产物。
"$VLLVM_CLANG" "${EXTRA_ARGS[@]}" -g0 -O0 -S -emit-llvm "$SRC" \
  -o "$OUT_DIR/test_bb2func.ll"
"$VLLVM_CLANG" "${EXTRA_ARGS[@]}" -g0 -O0 "$SRC" -o "$OUT_DIR/test_bb2func"
OBF_OUT=$("$OUT_DIR/test_bb2func")
echo "$OBF_OUT"

if [ "$BASE_OUT" != "$OBF_OUT" ]; then
  echo "bb2func runtime output mismatch: base='$BASE_OUT' obf='$OBF_OUT'" >&2
  exit 1
fi

LL="$OUT_DIR/test_bb2func.ll"

# 断言：至少提取出一个 internal helper，命名带 vllvm.bb2f 前缀；
# noinline/optnone 放在 attribute group 里，单独断言。
grep -Eq 'define internal[^@]*@vllvm\.bb2f\.' "$LL"
grep -Eq '^attributes #[0-9]+ = \{[^}]*noinline' "$LL"

# 断言：原函数里存在对 helper 的真实调用（调用边界必须留在 IR 中）。
grep -q 'call.*@vllvm\.bb2f\.' "$LL"

echo "test_bb2func passed"
