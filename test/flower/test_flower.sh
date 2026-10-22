#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
SRC="$ROOT_DIR/test/flower/test_flower.c"
OUT_DIR="${OUT_DIR:-$ROOT_DIR/test/flower/out}"

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

# 花指令计数：每条花指令是首行带 `<注释符> vllvm.flower` 标记的 inline
# asm 调用，标记只出现在 IR 的 asm 字符串里（注释不进目标文件），每条
# 调用恰好一次。
count_marks() { grep -Ec 'vllvm\.flower' "$1" || true; }

# 基线：无标注。
"$VLLVM_CLANG" "${EXTRA_ARGS[@]}" -g0 -O0 "$SRC" -o "$OUT_DIR/base"
set +e
BASE_OUT=$("$OUT_DIR/base")
BASE_STATUS=$?
set -e

# 默认 level=1：每个标注函数 1 条花指令（fixture 标注 mix/accumulate
# 两个函数，共 2 条）。
"$VLLVM_CLANG" "${EXTRA_ARGS[@]}" -g0 -O0 -fno-discard-value-names \
  -DVLLVM_TEST_FLOWER=1 "$SRC" -S -emit-llvm \
  -o "$OUT_DIR/flower.ll" 2>"$OUT_DIR/flower.log"
"$VLLVM_CLANG" "${EXTRA_ARGS[@]}" -g0 -O0 \
  -DVLLVM_TEST_FLOWER=1 "$SRC" -o "$OUT_DIR/flower"
set +e
FLOWER_OUT=$("$OUT_DIR/flower")
FLOWER_STATUS=$?
set -e

if [ "$FLOWER_OUT" != "$BASE_OUT" ] || [ "$FLOWER_STATUS" -ne "$BASE_STATUS" ]; then
  echo "flower output mismatch: base='$BASE_OUT'/$BASE_STATUS flower='$FLOWER_OUT'/$FLOWER_STATUS" >&2
  exit 1
fi

grep -q '\[vllvm\] VLLVMConfig:flower: level=1 (default=1 max=32)' "$OUT_DIR/flower.log"
grep -q '\[vllvm\] FlowerPass:mix' "$OUT_DIR/flower.log"
grep -q '\[vllvm\] FlowerPass:accumulate' "$OUT_DIR/flower.log"
if [ "$(count_marks "$OUT_DIR/flower.ll")" -ne 2 ]; then
  echo "expected exactly 2 flower marks (1 per annotated function), got $(count_marks "$OUT_DIR/flower.ll")" >&2
  exit 1
fi
# flower 独立于 vmfla 链路，不产生常量表。
if grep -q "vllvm.vmfla.const.table" "$OUT_DIR/flower.ll"; then
  echo "flower must stay independent of the vmfla chain" >&2
  exit 1
fi

# level=4：每个标注函数插入 4 条，共 8 条。
"$VLLVM_CLANG" "${EXTRA_ARGS[@]}" -g0 -O0 -fno-discard-value-names \
  -DVLLVM_TEST_FLOWER=1 -mllvm -vllvm-config=flower=4 \
  "$SRC" -S -emit-llvm -o "$OUT_DIR/flower4.ll" 2>"$OUT_DIR/flower4.log"
"$VLLVM_CLANG" "${EXTRA_ARGS[@]}" -g0 -O0 \
  -DVLLVM_TEST_FLOWER=1 -mllvm -vllvm-config=flower=4 \
  "$SRC" -o "$OUT_DIR/flower4"
set +e
FLOWER4_OUT=$("$OUT_DIR/flower4")
FLOWER4_STATUS=$?
set -e

if [ "$FLOWER4_OUT" != "$BASE_OUT" ] || [ "$FLOWER4_STATUS" -ne "$BASE_STATUS" ]; then
  echo "flower level=4 output mismatch: '$FLOWER4_OUT'/$FLOWER4_STATUS" >&2
  exit 1
fi
grep -q '\[vllvm\] VLLVMConfig:flower: level=4 (default=1 max=32)' "$OUT_DIR/flower4.log"
if [ "$(count_marks "$OUT_DIR/flower4.ll")" -ne 8 ]; then
  echo "expected exactly 8 flower marks at level=4, got $(count_marks "$OUT_DIR/flower4.ll")" >&2
  exit 1
fi

# 越界等级钳制到 max=32：每函数 32 条，共 64 条。
"$VLLVM_CLANG" "${EXTRA_ARGS[@]}" -g0 -O0 -fno-discard-value-names \
  -DVLLVM_TEST_FLOWER=1 -mllvm -vllvm-config=flower=999 \
  "$SRC" -S -emit-llvm -o "$OUT_DIR/flower_cap.ll" 2>"$OUT_DIR/flower_cap.log"
grep -q '\[vllvm\] VLLVMConfig:flower: level clamped to 32' "$OUT_DIR/flower_cap.log"
if [ "$(count_marks "$OUT_DIR/flower_cap.ll")" -ne 64 ]; then
  echo "expected 64 flower marks at clamped level=32, got $(count_marks "$OUT_DIR/flower_cap.ll")" >&2
  exit 1
fi

# level=0：flower 全局关闭，IR 无任何花指令（注解字符串是冒号分隔的
# "vllvm:flower"，不会被带转义点的标记匹配误中）。
"$VLLVM_CLANG" "${EXTRA_ARGS[@]}" -g0 -O0 -fno-discard-value-names \
  -DVLLVM_TEST_FLOWER=1 -mllvm -vllvm-config=flower=0 \
  "$SRC" -S -emit-llvm -o "$OUT_DIR/off.ll" 2>"$OUT_DIR/off.log"
if grep -q "vllvm\.flower" "$OUT_DIR/off.ll"; then
  echo "flower=0 unexpectedly inserted junk" >&2
  exit 1
fi
grep -q '\[vllvm\] VLLVMConfig:flower: level=0 (default=1 max=32)' "$OUT_DIR/off.log"

# 组合：flower+bcf 同函数。花指令先插入真实代码，bcf 后行（其克隆环
# 节排除 CallBase，不会克隆花指令 asm 调用），计数只断言下界；bcf 自身
# 产物（虚假块/共享 seed）必须同时存在。
"$VLLVM_CLANG" "${EXTRA_ARGS[@]}" -g0 -O0 -fno-discard-value-names \
  -DVLLVM_TEST_FLOWER_COMBO=1 "$SRC" -S -emit-llvm \
  -o "$OUT_DIR/combo.ll" 2>"$OUT_DIR/combo.log"
"$VLLVM_CLANG" "${EXTRA_ARGS[@]}" -g0 -O0 \
  -DVLLVM_TEST_FLOWER_COMBO=1 "$SRC" -o "$OUT_DIR/combo"
set +e
COMBO_OUT=$("$OUT_DIR/combo")
COMBO_STATUS=$?
set -e

if [ "$COMBO_OUT" != "$BASE_OUT" ] || [ "$COMBO_STATUS" -ne "$BASE_STATUS" ]; then
  echo "flower+bcf combo output mismatch: '$COMBO_OUT'/$COMBO_STATUS" >&2
  exit 1
fi
if [ "$(count_marks "$OUT_DIR/combo.ll")" -lt 2 ]; then
  echo "flower+bcf combo lost flower marks" >&2
  exit 1
fi
grep -Eq "^vllvm\.bcf\.fake\." "$OUT_DIR/combo.ll"
grep -q "@export_write" "$OUT_DIR/combo.ll"

echo "flower tests passed"
