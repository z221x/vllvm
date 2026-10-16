#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
SRC="$ROOT_DIR/test/vmfla/test_vmfla_indirectbr.c"
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
if [ "$(uname -s)" = Darwin ] && command -v xcrun >/dev/null 2>&1; then
  SDK_PATH=$(xcrun --show-sdk-path 2>/dev/null || true)
  if [ -n "$SDK_PATH" ]; then
    EXTRA_ARGS+=(-isysroot "$SDK_PATH")
  fi
fi

mkdir -p "$OUT_DIR"

"$VLLVM_CLANG" "${EXTRA_ARGS[@]}" -g0 -O0 "$SRC" -o "$OUT_DIR/indirectbr_base"
BASE_OUT=$("$OUT_DIR/indirectbr_base")

"$VLLVM_CLANG" "${EXTRA_ARGS[@]}" -g0 -O0 -fno-discard-value-names -S \
  -emit-llvm -DVLLVM_TEST_VMFLA=1 "$SRC" \
  -o "$OUT_DIR/test_vmfla_indirectbr.ll"
"$VLLVM_CLANG" "${EXTRA_ARGS[@]}" -g0 -O0 -DVLLVM_TEST_VMFLA=1 "$SRC" \
  -o "$OUT_DIR/test_vmfla_indirectbr"

# 状态机错编译在 bench 级程序上表现为死循环或段错误；小用例上用
# 超时兜底，避免回归时脚本挂死。
OBF_OUT=$(python3 - "$OUT_DIR/test_vmfla_indirectbr" <<'PY'
import subprocess, sys
try:
    p = subprocess.run([sys.argv[1]], capture_output=True, text=True,
                       timeout=30)
    sys.stdout.write(p.stdout)
    sys.exit(p.returncode)
except subprocess.TimeoutExpired:
    sys.stderr.write("vmfla indirectbr binary timed out\n")
    sys.exit(124)
PY
)

if [ "$BASE_OUT" != "$OBF_OUT" ]; then
  echo "vmfla indirectbr output mismatch: base='$BASE_OUT' obf='$OBF_OUT'" >&2
  exit 1
fi

LL="$OUT_DIR/test_vmfla_indirectbr.ll"

# computed goto 的 indirectbr 必须保留：dispatch 表仍是地址跳转，
# 平坦化不能把它改写成线性链。
grep -q "indirectbr" "$LL"

# 间接跳转直连的 case 头部必须写回自身表下标（init store 之外还有
# repair store），否则增量状态更新以旧下标为基准算出错误状态。
REPAIR_CNT=$(grep -Ec "store volatile i32 [0-9]+, ptr %.?switchVar" "$LL")
if [ "$REPAIR_CNT" -lt 2 ]; then
  echo "expected >=2 constant runtime-index stores (init + repair), got $REPAIR_CNT" >&2
  exit 1
fi

echo "test_vmfla_indirectbr passed"
