#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
BCF_SRC="$ROOT_DIR/test/boguscontrolflow/test_bcf.c"
VMFLA_SRC="$ROOT_DIR/test/localvarstruct/test_lvars.c"
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

# vmfla 等级标记：局部变量结构化结构体；bcf 等级标记：虚假块标签。
count_lvars() { grep -c "vllvm\.localvars\." "$1" || true; }
count_bcf_fake() { grep -c "^vllvm\.bcf\.fake\." "$1" || true; }

EXTRA_ARGS=()
if [ "$(uname -s)" = Darwin ] && command -v xcrun >/dev/null 2>&1; then
  SDK_PATH=$(xcrun --show-sdk-path 2>/dev/null || true)
  if [ -n "$SDK_PATH" ]; then
    EXTRA_ARGS+=(-isysroot "$SDK_PATH")
  fi
fi

mkdir -p "$OUT_DIR"

# vmfla 等级段用 vmfla 注解的 fixture；bcf 已独立，等级段用 bcf 注解的
# test_bcf.c。两个 main 都以校验和低位作退出码，非零退出码是预期行为，
# 用 set +e 捕获输出和退出码再比较。
"$VLLVM_CLANG" "${EXTRA_ARGS[@]}" -g0 -O0 "$VMFLA_SRC" -o "$OUT_DIR/base"
"$VLLVM_CLANG" "${EXTRA_ARGS[@]}" -g0 -O0 "$BCF_SRC" -o "$OUT_DIR/bcf_base"
set +e
BASE_OUT=$("$OUT_DIR/base")
BASE_STATUS=$?
BCF_BASE_OUT=$("$OUT_DIR/bcf_base")
BCF_BASE_STATUS=$?
set -e

# vmfla level=0：整条链路全局关闭，IR 无任何链路产物（表/helper/dispatcher）。
"$VLLVM_CLANG" "${EXTRA_ARGS[@]}" -g0 -O0 -fno-discard-value-names \
  -DVLLVM_TEST_VMFLA=1 -mllvm -vllvm-config=vmfla=0 \
  "$VMFLA_SRC" -S -emit-llvm -o "$OUT_DIR/off.ll" 2>"$OUT_DIR/off.log"
if grep -Eq "vllvm\.(vmfla\.const\.table|bb2f\.|merge\.)" "$OUT_DIR/off.ll"; then
  echo "vmfla=0 unexpectedly transformed the module" >&2
  exit 1
fi
grep -q '\[vllvm\] VLLVMConfig:vmfla: level=0 (default=1 max=2)' "$OUT_DIR/off.log"

# vmfla level=1（默认）：bb2func + merge + 平坦化；不做局部变量结构化。
"$VLLVM_CLANG" "${EXTRA_ARGS[@]}" -g0 -O0 -fno-discard-value-names \
  -DVLLVM_TEST_VMFLA=1 "$VMFLA_SRC" -S -emit-llvm \
  -o "$OUT_DIR/l1.ll" 2>"$OUT_DIR/l1.log"
grep -q '\[vllvm\] VLLVMConfig:vmfla: level=1 (default=1 max=2)' "$OUT_DIR/l1.log"
grep -q "vllvm\.vmfla\.const\.table" "$OUT_DIR/l1.ll"
[ "$(count_lvars "$OUT_DIR/l1.ll")" -eq 0 ] || {
  echo "vmfla=1 must not enable local var structuring" >&2
  exit 1
}

# vmfla level=2：再开启局部变量结构化，运行结果不变。
"$VLLVM_CLANG" "${EXTRA_ARGS[@]}" -g0 -O0 -fno-discard-value-names \
  -DVLLVM_TEST_VMFLA=1 -mllvm -vllvm-config=vmfla=2 \
  "$VMFLA_SRC" -S -emit-llvm -o "$OUT_DIR/l2.ll" 2>"$OUT_DIR/l2.log"
grep -q '\[vllvm\] VLLVMConfig:vmfla: level=2 (default=1 max=2)' "$OUT_DIR/l2.log"
[ "$(count_lvars "$OUT_DIR/l2.ll")" -gt 0 ] || {
  echo "vmfla=2 produced no local var structuring" >&2
  exit 1
}

"$VLLVM_CLANG" "${EXTRA_ARGS[@]}" -g0 -O0 -DVLLVM_TEST_VMFLA=1 -mllvm -vllvm-config=vmfla=2 \
  "$VMFLA_SRC" -o "$OUT_DIR/l2"
set +e
L2_OUT=$("$OUT_DIR/l2")
L2_STATUS=$?
set -e
if [ "$BASE_OUT" != "$L2_OUT" ] || [ "$BASE_STATUS" -ne "$L2_STATUS" ]; then
  echo "vmfla=2 runtime output mismatch" >&2
  exit 1
fi

# 越界等级被钳制到 max 并告警。
"$VLLVM_CLANG" "${EXTRA_ARGS[@]}" -g0 -O0 -DVLLVM_TEST_VMFLA=1 -mllvm -vllvm-config=vmfla=9 \
  "$VMFLA_SRC" -S -emit-llvm -o /dev/null 2>"$OUT_DIR/clamp.log"
grep -q '\[vllvm\] VLLVMConfig:vmfla: level clamped to 2' "$OUT_DIR/clamp.log"

# 非法片段：告警并跳过，不影响其他项（vmfla 仍按显式的 2 生效）。
"$VLLVM_CLANG" "${EXTRA_ARGS[@]}" -g0 -O0 -DVLLVM_TEST_VMFLA=1 \
  -mllvm -vllvm-config=baditem,vmfla=2 \
  "$VMFLA_SRC" -S -emit-llvm -o /dev/null 2>"$OUT_DIR/bad.log"
grep -q "\[vllvm\] VLLVMConfig: ignored bad item 'baditem'" "$OUT_DIR/bad.log"
grep -q '\[vllvm\] VLLVMConfig:vmfla: level=2 (default=1 max=2)' "$OUT_DIR/bad.log"

# bcf 独立 pass 等级：0 关闭、默认开启、钳制到 3。
"$VLLVM_CLANG" "${EXTRA_ARGS[@]}" -g0 -O0 -fno-discard-value-names \
  -DVLLVM_TEST_BCF=1 -mllvm -vllvm-config=bcf=0 \
  "$BCF_SRC" -S -emit-llvm -o "$OUT_DIR/bcf_off.ll" 2>"$OUT_DIR/bcf_off.log"
if grep -Eq "@export_write|vllvm\.bcf\.(seed|fake|cmp)" "$OUT_DIR/bcf_off.ll"; then
  echo "bcf=0 unexpectedly transformed the module" >&2
  exit 1
fi
grep -q '\[vllvm\] VLLVMConfig:bcf: level=0 (default=1 max=3)' "$OUT_DIR/bcf_off.log"

"$VLLVM_CLANG" "${EXTRA_ARGS[@]}" -g0 -O0 -fno-discard-value-names \
  -DVLLVM_TEST_BCF=1 "$BCF_SRC" -S -emit-llvm -o "$OUT_DIR/bcf_l1.ll" 2>"$OUT_DIR/bcf_l1.log"
grep -q '\[vllvm\] VLLVMConfig:bcf: level=1 (default=1 max=3)' "$OUT_DIR/bcf_l1.log"
grep -q "@export_write" "$OUT_DIR/bcf_l1.ll"
[ "$(count_bcf_fake "$OUT_DIR/bcf_l1.ll")" -gt 0 ] || {
  echo "bcf default level produced no bogus flow" >&2
  exit 1
}

"$VLLVM_CLANG" "${EXTRA_ARGS[@]}" -g0 -O0 -DVLLVM_TEST_BCF=1 "$BCF_SRC" \
  -mllvm -vllvm-config=bcf=9 -S -emit-llvm -o /dev/null 2>"$OUT_DIR/bcf_clamp.log"
"$VLLVM_CLANG" "${EXTRA_ARGS[@]}" -g0 -O0 -DVLLVM_TEST_BCF=1 "$BCF_SRC" \
  -mllvm -vllvm-config=bcf=9 -o "$OUT_DIR/bcf_l3"
set +e
BCF_L3_OUT=$("$OUT_DIR/bcf_l3")
BCF_L3_STATUS=$?
set -e
if [ "$BCF_BASE_OUT" != "$BCF_L3_OUT" ] || [ "$BCF_BASE_STATUS" -ne "$BCF_L3_STATUS" ]; then
  echo "bcf=3 runtime output mismatch" >&2
  exit 1
fi
grep -q '\[vllvm\] VLLVMConfig:bcf: level clamped to 3' "$OUT_DIR/bcf_clamp.log"

echo "config: vmfla=0/1/2 ok, bcf=0/1/clamp ok, bad-item ok"
