#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
SRC="$ROOT_DIR/test/boguscontrolflow/test_bcf.c"
OUT_DIR=${OUT_DIR:-"$ROOT_DIR/test/config/out"}

if [ -n "${CLANG:-}" ]; then
  VLLVM_CLANG="$CLANG"
elif [ -x "$ROOT_DIR/build/llvm-macos/bin/clang" ]; then
  VLLVM_CLANG="$ROOT_DIR/build/llvm-macos/bin/clang"
elif [ -x "$ROOT_DIR/build/llvm-linux/bin/clang" ]; then
  VLLVM_CLANG="$ROOT_DIR/build/llvm-linux/bin/clang"
else
  VLLVM_CLANG="clang"
fi

count_bcf() { grep -c "vllvm\.bcf" "$1" || true; }

mkdir -p "$OUT_DIR"

"$VLLVM_CLANG" -g0 -O0 "$SRC" -o "$OUT_DIR/base"

# test_bcf.c 的 main 返回校验和低 8 位，非零退出码是预期行为；
# 与 test_bcf.sh 一致地用 set +e 捕获输出和退出码再比较。
set +e
BASE_OUT=$("$OUT_DIR/base")
BASE_STATUS=$?
set -e

# level=0：bcf 全局关闭，IR 无任何 vllvm.bcf 产物，且公告行记录 level=0。
"$VLLVM_CLANG" -g0 -O0 -DVLLVM_TEST_BCF=1 -mllvm -vllvm-config=bcf=0 \
  "$SRC" -S -emit-llvm -o "$OUT_DIR/off.ll" 2>"$OUT_DIR/off.log"
[ "$(count_bcf "$OUT_DIR/off.ll")" -eq 0 ] || {
  echo "bcf=0 unexpectedly transformed the module" >&2
  exit 1
}
grep -q '\[vllvm\] VLLVMConfig:bcf: level=0 (default=1 max=3)' "$OUT_DIR/off.log"

# 默认（level=1）：照常混淆，运行结果与基准一致。
"$VLLVM_CLANG" -g0 -O0 -DVLLVM_TEST_BCF=1 "$SRC" -S -emit-llvm \
  -o "$OUT_DIR/l1.ll" 2>"$OUT_DIR/l1.log"
grep -q '\[vllvm\] VLLVMConfig:bcf: level=1 (default=1 max=3)' "$OUT_DIR/l1.log"
L1_COUNT=$(count_bcf "$OUT_DIR/l1.ll")
[ "$L1_COUNT" -gt 0 ] || {
  echo "bcf default level produced no bogus flow" >&2
  exit 1
}

# level=3：三轮 + 全量覆盖，虚假流规模必须大于默认档，运行结果不变。
"$VLLVM_CLANG" -g0 -O0 -DVLLVM_TEST_BCF=1 -mllvm -vllvm-config=bcf=3 \
  "$SRC" -S -emit-llvm -o "$OUT_DIR/l3.ll" 2>"$OUT_DIR/l3.log"
grep -q '\[vllvm\] VLLVMConfig:bcf: level=3 (default=1 max=3)' "$OUT_DIR/l3.log"
L3_COUNT=$(count_bcf "$OUT_DIR/l3.ll")
[ "$L3_COUNT" -gt "$L1_COUNT" ] || {
  echo "bcf=3 ($L3_COUNT) did not exceed bcf=1 ($L1_COUNT)" >&2
  exit 1
}

"$VLLVM_CLANG" -g0 -O0 -DVLLVM_TEST_BCF=1 -mllvm -vllvm-config=bcf=3 \
  "$SRC" -o "$OUT_DIR/l3"
set +e
L3_OUT=$("$OUT_DIR/l3")
L3_STATUS=$?
set -e
if [ "$BASE_OUT" != "$L3_OUT" ] || [ "$BASE_STATUS" -ne "$L3_STATUS" ]; then
  echo "bcf=3 runtime output mismatch" >&2
  exit 1
fi

# 越界等级被钳制到 max 并告警。
"$VLLVM_CLANG" -g0 -O0 -DVLLVM_TEST_BCF=1 -mllvm -vllvm-config=bcf=9 \
  "$SRC" -S -emit-llvm -o /dev/null 2>"$OUT_DIR/clamp.log"
grep -q '\[vllvm\] VLLVMConfig:bcf: level clamped to 3' "$OUT_DIR/clamp.log"

# 非法片段：告警并跳过，不影响其他项（bcf 仍按显式的 2 生效）。
"$VLLVM_CLANG" -g0 -O0 -DVLLVM_TEST_BCF=1 \
  -mllvm -vllvm-config=baditem,bcf=2 \
  "$SRC" -S -emit-llvm -o /dev/null 2>"$OUT_DIR/bad.log"
grep -q "\[vllvm\] VLLVMConfig: ignored bad item 'baditem'" "$OUT_DIR/bad.log"
grep -q '\[vllvm\] VLLVMConfig:bcf: level=2 (default=1 max=3)' "$OUT_DIR/bad.log"

echo "config: bcf=0 off, level1=$L1_COUNT, level3=$L3_COUNT, clamp+bad-item ok"
