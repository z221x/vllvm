#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
OUT_DIR="${OUT_DIR:-$ROOT_DIR/test/config/out}"

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
if [ "$(uname -s)" = Darwin ] && command -v xcrun >/dev/null 2>&1; then
  SDK_PATH=$(xcrun --show-sdk-path 2>/dev/null || true)
  if [ -n "$SDK_PATH" ]; then
    EXTRA_ARGS+=(-isysroot "$SDK_PATH")
  fi
fi

mkdir -p "$OUT_DIR"

# 混淆不允许与编译优化同用：所有 vllvm 标注在 >O0 下必须编译失败且
# 不产出二进制；无标注的普通优化编译不受影响。
SRC="$ROOT_DIR/test/config/test_obf_requires_o0.c"

for OLEVEL in O1 O2 O3 Os; do
  set +e
  "$VLLVM_CLANG" "${EXTRA_ARGS[@]}" -g0 "-$OLEVEL" -DVLLVM_TEST_OBF=1 \
    "$SRC" -o "$OUT_DIR/obf_$OLEVEL" 2>"$OUT_DIR/obf_$OLEVEL.log"
  RC=$?
  set -e
  if [ "$RC" -eq 0 ]; then
    echo "annotated build must fail at -$OLEVEL" >&2
    exit 1
  fi
  if [ -f "$OUT_DIR/obf_$OLEVEL" ]; then
    echo "annotated build at -$OLEVEL must not produce a binary" >&2
    exit 1
  fi
  grep -q "cannot be combined with optimization" \
    "$OUT_DIR/obf_$OLEVEL.log" || {
    echo "missing -O0 guidance diagnostic at -$OLEVEL" >&2
    exit 1
  }
done

# 对照一：-O0 + 标注正常产出且运行正确。
"$VLLVM_CLANG" "${EXTRA_ARGS[@]}" -g0 -O0 -DVLLVM_TEST_OBF=1 "$SRC" \
  -o "$OUT_DIR/obf_O0"
set +e
"$OUT_DIR/obf_O0"
OBF_RC=$?
set -e

# 对照二：无标注的 -O2 编译不受约束。
"$VLLVM_CLANG" "${EXTRA_ARGS[@]}" -g0 -O2 "$SRC" \
  -o "$OUT_DIR/plain_O2"
set +e
"$OUT_DIR/plain_O2"
PLAIN_RC=$?
set -e

if [ "$OBF_RC" -ne 0 ] || [ "$PLAIN_RC" -ne 0 ]; then
  echo "runtime mismatch: obf_O0=$OBF_RC plain_O2=$PLAIN_RC" >&2
  exit 1
fi

echo "test_obf_requires_o0 passed"
