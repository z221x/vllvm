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

# 断言第一段：vmfla 链路当前只跑平坦化本体，不接 bb2func/merge 前段
# （两 pass 作为保留方案独立标注使用），产物中不得出现两者的痕迹。
if grep -Eq '@vllvm\.bb2f\.|@vllvm\.merge\.' "$LL"; then
  echo "vmfla chain must not emit bb2func/merge artifacts" >&2
  exit 1
fi

# 断言第二段：vmfla 常量表和 impl 拆分仍在，wrapper 是直连调用点
# （无伪调用守卫）。
grep -q 'vllvm\.vmfla\.const\.table' "$LL"
grep -Eq 'define [^@]*@[A-Za-z0-9_]+\.vllvm\.impl\(' "$LL"
grep -Eq 'call [^@]*@[A-Za-z0-9_]+\.vllvm\.impl\([^)]*vllvm\.vmfla\.const\.table' "$LL"
if grep -q "vllvm.tablesink" "$LL"; then
  echo "vmfla entry must be a single direct call" >&2
  exit 1
fi
if grep -q 'func_table' "$LL"; then
  echo "vmfla chain must not emit a func_table" >&2
  exit 1
fi

echo "test_vmfla_chain passed"
