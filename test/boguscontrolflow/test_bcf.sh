#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
SRC="$ROOT_DIR/test/boguscontrolflow/test_bcf.c"
OUT_DIR="${OUT_DIR:-$ROOT_DIR/test/boguscontrolflow/out}"

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

"$VLLVM_CLANG" "${EXTRA_ARGS[@]}" -g0 -O0 "$SRC" -o "$OUT_DIR/base"
set +e
"$OUT_DIR/base"
BASE_STATUS=$?
set -e

"$VLLVM_CLANG" "${EXTRA_ARGS[@]}" -g0 -O0 -fno-discard-value-names -S -emit-llvm \
  -DVLLVM_TEST_BCF=1 "$SRC" -o "$OUT_DIR/bcf.ll"

"$VLLVM_CLANG" "${EXTRA_ARGS[@]}" -g0 -O0 \
  -DVLLVM_TEST_BCF=1 "$SRC" -o "$OUT_DIR/bcf"
set +e
"$OUT_DIR/bcf"
BCF_STATUS=$?
set -e

if [ "$BCF_STATUS" -ne "$BASE_STATUS" ]; then
  echo "bcf exit status mismatch: base=$BASE_STATUS pass=$BCF_STATUS" >&2
  exit 1
fi

# 独立 BCF：模块级共享导出 seed（所有 bcf 函数共用，函数内不初始化，
# 符号名 export_write 不带 vllvm 前缀）
# + 单后继分支伪造；随机指令虚假块存在，谓词 (seed op K) ==/!= 随机
# 常量（避开默认值 0 过同一运算的期望结果），极性恒定（== 恒假 /
# != 恒真，真假边排布对调），真边回原后继。bcf 独立于 vmfla，不允许
# 触发 vmfla 常量表。
grep -q "@export_write" "$OUT_DIR/bcf.ll"
grep -Eq "^vllvm\.bcf\.fake\." "$OUT_DIR/bcf.ll"
grep -q "vllvm.bcf.cmp" "$OUT_DIR/bcf.ll"
grep -Eq "icmp (eq|ne)" "$OUT_DIR/bcf.ll"
if grep -q "vllvm.vmfla.const.table" "$OUT_DIR/bcf.ll"; then
  echo "bcf must stay independent of the vmfla chain" >&2
  exit 1
fi

# seed 是模块级导出全局 export_write（WeakAnyLinkage：IR 定义行带 weak
# 且不带 internal/private 前缀，多 TU 各自弱定义、链接器合并为全程序
# 同一变量；所有 bcf 函数共享同一符号，外部库可按符号名引用），且函数
# 内不再初始化——不存在 ptrtoint 归一推导链；改写点谓词是
# (seed op K) ==/!= 随机常量：比较对象为过非零运算的 enc 值与常量
# （无 x == x + c 自比较签名），常量避开默认值 0 过同一运算的期望
# 结果，默认值下 == 恒假 / != 恒真；volatile 读保证反编译器无法
# 折叠，恒定谓词与虚假块必须留在反编译视图里。
if [ "$(grep -Ec '^@export_write = weak global i16 0' "$OUT_DIR/bcf.ll")" -ne 1 ]; then
  echo "expected exactly one shared weak seed global" >&2
  exit 1
fi
if grep -q '^@export_write\.' "$OUT_DIR/bcf.ll"; then
  echo "seed must be a single shared global, no per-function variants" >&2
  exit 1
fi
if grep -Eq 'vllvm\.bcf\.seed\.(addr|init|x|not|norm)' "$OUT_DIR/bcf.ll"; then
  echo "seed must not be initialized inside the function" >&2
  exit 1
fi
grep -q "load volatile i16, ptr @export_write" "$OUT_DIR/bcf.ll"
grep -Eq 'icmp (eq|ne) i16 %vllvm\.bcf\.enc\.(add|sub|xor)[0-9]*, -?[0-9]+' "$OUT_DIR/bcf.ll"
if grep -Eq 'icmp (eq|ne) i16 %vllvm\.bcf\.seed\.' "$OUT_DIR/bcf.ll"; then
  echo "predicate must compare the encrypted value against a constant" >&2
  exit 1
fi
if grep -Eq "store volatile i(16|32) [0-9-]+, ptr @export_write" "$OUT_DIR/bcf.ll"; then
  echo "seed must be derived opaquely, not stored as a constant" >&2
  exit 1
fi
# 虚假块必须被至少一条永假分支引用（锚定），否则不可达块不进后端。
grep -q "label %vllvm.bcf.fake" "$OUT_DIR/bcf.ll"

# 前置处理 1：大块切割产生数字后缀碎片块（entry 的尾段）。
grep -Eq "^entry[0-9]+:" "$OUT_DIR/bcf.ll"
# 加密链只用 add/sub/xor，不允许出现乘法。
if grep -Eq "mul i16 %vllvm\.bcf\.|vllvm\.bcf\.enc\.mul" "$OUT_DIR/bcf.ll"; then
  echo "encryption chain must not use multiplication" >&2
  exit 1
fi
# 前置处理 3：全块重排——虚假块不聚集在函数尾部（未重排时连续相邻）。
FAKE_POS=($(grep -n "^vllvm\.bcf\.fake\." "$OUT_DIR/bcf.ll" | cut -d: -f1))
ADJACENT=1
for ((i=1; i<${#FAKE_POS[@]}; i++)); do
  if (( FAKE_POS[i] - FAKE_POS[i-1] > 1 )); then ADJACENT=0; break; fi
done
if [ "$ADJACENT" = "1" ]; then
  echo "junk blocks must be shuffled among real blocks" >&2
  exit 1
fi
