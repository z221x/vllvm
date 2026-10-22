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

EXTRA_ARGS=(-Xclang -llvm-verify-each)
if [ "$(uname -s)" = "Darwin" ] && command -v xcrun >/dev/null 2>&1; then
  SDK_PATH=$(xcrun --show-sdk-path 2>/dev/null || true)
  if [ -n "$SDK_PATH" ]; then
    EXTRA_ARGS+=(-isysroot "$SDK_PATH")
  fi
fi

mkdir -p "$OUT_DIR"

# 旧名/别名已删除（支持集只有 bcf/vmfla/icall/enstr/ibr/vmp）：
# vllvm:fla,lvars 之类的旧标注必须 (1) 逐名告警 unknown annotation kind，
# (2) 不再触发任何混淆产物（无 vmfla 常量表/结构化痕迹），
# (3) 编译照常成功且运行行为与 base 一致。
"$VLLVM_CLANG" "${EXTRA_ARGS[@]}" -g0 -O0 -S -emit-llvm \
  -DVLLVM_TEST_LEGACY_NAMES=1 "$SRC" \
  -o "$OUT_DIR/test_lvars_legacy.ll" 2>"$OUT_DIR/test_lvars_legacy.log"

grep -q "unknown annotation kind 'fla' ignored" "$OUT_DIR/test_lvars_legacy.log"
grep -q "unknown annotation kind 'lvars' ignored" "$OUT_DIR/test_lvars_legacy.log"

if grep -Eq 'vllvm\.(vmfla\.const\.table|localvars\.)|load volatile' \
  "$OUT_DIR/test_lvars_legacy.ll"; then
  echo "legacy names must not trigger any obfuscation artifacts" >&2
  exit 1
fi

"$VLLVM_CLANG" "${EXTRA_ARGS[@]}" -g0 -O0 "$SRC" -o "$OUT_DIR/legacy_base"
"$VLLVM_CLANG" "${EXTRA_ARGS[@]}" -g0 -O0 \
  -DVLLVM_TEST_LEGACY_NAMES=1 "$SRC" -o "$OUT_DIR/legacy"

set +e
"$OUT_DIR/legacy_base"
BASE_STATUS=$?
"$OUT_DIR/legacy"
LEGACY_STATUS=$?
set -e

if [ "$BASE_STATUS" -ne "$LEGACY_STATUS" ]; then
  echo "exit status mismatch: base=$BASE_STATUS legacy=$LEGACY_STATUS" >&2
  exit 1
fi

echo "test_lvars_fla passed (legacy names rejected)"
