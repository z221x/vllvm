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

# 独立 BCF：栈 seed + 已知密文 + 单后继分支伪造；随机指令虚假块存在，
# 谓词 icmp eq（加密值 vs 不等随机数）永假，真边回原后继。bcf 独立于
# vmfla，不允许触发 vmfla 常量表。
grep -q "vllvm.bcf.seed" "$OUT_DIR/bcf.ll"
grep -Eq "^vllvm\.bcf\.fake\." "$OUT_DIR/bcf.ll"
grep -q "vllvm.bcf.cmp" "$OUT_DIR/bcf.ll"
grep -q "icmp eq" "$OUT_DIR/bcf.ll"
if grep -q "vllvm.vmfla.const.table" "$OUT_DIR/bcf.ll"; then
  echo "bcf must stay independent of the vmfla chain" >&2
  exit 1
fi

# seed 不能是明文常量：由槽位地址经 i16 上的 x + (x ^ -1) 归一（恒为
# 0xFFFF）后 volatile 压栈；全部运算走 uint16 回绕，反编译器无法折叠
# 密文链，永假谓词与虚假块必须留在反编译视图里。
grep -q "%vllvm.bcf.seed.addr = ptrtoint" "$OUT_DIR/bcf.ll"
grep -Eq "add i16 %vllvm\.bcf\.seed\.x, %vllvm\.bcf\.seed\.not" "$OUT_DIR/bcf.ll"
grep -q "icmp eq i16" "$OUT_DIR/bcf.ll"
if grep -Eq "store volatile i(16|32) [0-9-]+, ptr %vllvm\.bcf\.seed" "$OUT_DIR/bcf.ll"; then
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
