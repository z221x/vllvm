#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
SRC="$ROOT_DIR/test/vmfla/test_vmfla_chain.c"
OUT_DIR="${OUT_DIR:-$ROOT_DIR/test/vmfla/out}"

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
  -o "$OUT_DIR/test_vmfla_chain.ll"
"$VLLVM_CLANG" "${EXTRA_ARGS[@]}" -g0 -O0 "$SRC" -o "$OUT_DIR/test_vmfla_chain"
OBF_OUT=$("$OUT_DIR/test_vmfla_chain")
echo "$OBF_OUT"

if [ "$BASE_OUT" != "$OBF_OUT" ]; then
  echo "vmfla chain runtime output mismatch: base='$BASE_OUT' obf='$OBF_OUT'" >&2
  exit 1
fi

LL="$OUT_DIR/test_vmfla_chain.ll"

# 断言第一段：bb2func 提取的 helper 存在（noinline 在 attribute group 中）。
grep -Eq 'define internal[^@]*@vllvm\.bb2f\.' "$LL"
grep -Eq '^attributes #[0-9]+ = \{[^}]*noinline' "$LL"

# 断言第二段：merge dispatcher 存在；其调用点已被 vmfla 的 func_table
# 间接化，所以断言 dispatcher 地址进入函数表。
grep -Eq 'define internal[^@]*@vllvm\.merge\.' "$LL"
grep -q '@vllvm\.merge\.' "$LL"
grep -q 'func_table' "$LL"

# 断言第三段：vmfla 常量表和 impl 拆分仍在，说明 vmfla 在链路末尾执行。
grep -q 'vllvm\.vmfla\.const\.table' "$LL"
grep -q 'vllvm\.impl' "$LL"

echo "test_vmfla_chain passed"
