#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
SRC="$ROOT_DIR/test/indirectcall/test_indirectcall.c"
OUT_DIR="${OUT_DIR:-$ROOT_DIR/test/indirectcall/out}"

if [ -n "${CLANG:-}" ]; then
  VLLVM_CLANG="$CLANG"
elif [ -x "$ROOT_DIR/build/llvm-macos/bin/clang" ]; then
  VLLVM_CLANG="$ROOT_DIR/build/llvm-macos/bin/clang"
elif [ -x "$ROOT_DIR/build/llvm-linux/bin/clang" ]; then
  VLLVM_CLANG="$ROOT_DIR/build/llvm-linux/bin/clang"
else
  VLLVM_CLANG=clang
fi

mkdir -p "$OUT_DIR"

TARGET_ARGS=(-target aarch64-unknown-linux-gnu -Xclang -llvm-verify-each)
LINK_ARGS=(-Xclang -llvm-verify-each)
if [ "$(uname -s)" = Darwin ]; then
  SDK_PATH=$(xcrun --show-sdk-path)
  TARGET_ARGS=(-target arm64-apple-macos -isysroot "$SDK_PATH" -Xclang -llvm-verify-each)
  LINK_ARGS=(-isysroot "$SDK_PATH" -Xclang -llvm-verify-each)
fi

"$VLLVM_CLANG" "${TARGET_ARGS[@]}" -O0 -S -emit-llvm -fno-discard-value-names \
  -DVLLVM_TEST_ICALL=1 "$SRC" -o "$OUT_DIR/icall.ll"
"$VLLVM_CLANG" "${TARGET_ARGS[@]}" -O0 -S \
  -DVLLVM_TEST_ICALL=1 "$SRC" -o "$OUT_DIR/icall.s"

# 跳板为 naked 汇编函数（icallcc 约定）：加密链就地改写 x0~x7；调用号
# 由调用点常量经 i32 nest 送入 w19，跳板用 ubfx/and 从 w19 解码查池并
# ret x19 跳转（与 src/funccaller 的 call_func 一致）。
grep -Eq "define internal icallcc .*@__vllvm_icall\.[0-9A-F]+\.call\." "$OUT_DIR/icall.ll"
grep -q "call icallcc" "$OUT_DIR/icall.ll"
grep -Eq "i32 nest [0-9]+" "$OUT_DIR/icall.ll"
grep -q "ret x19" "$OUT_DIR/icall.ll"
grep -Eq "ret[[:space:]]+x19" "$OUT_DIR/icall.s"
grep -Eq "ubfx[[:space:]]+w16, w19, #16, #8" "$OUT_DIR/icall.s"
grep -Eq "mov[[:space:]]+w19," "$OUT_DIR/icall.s"
if grep -q "vllvm.icall.index_array" "$OUT_DIR/icall.ll"; then
  echo "icall packed indexes must be trampoline constants" >&2
  exit 1
fi
grep -q "register_func" "$OUT_DIR/icall.ll"
grep -q "create_func_pool" "$OUT_DIR/icall.ll"
grep -Eq "i32 0, ptr @__vllvm_icall\..*\.register_funcs" "$OUT_DIR/icall.ll"

# —— level 1（默认）：仅专属跳板间接化（icallcc/nest/w19/ret x19 查表），
# 不启用参数加密。
if grep -q "vllvm.icall.crypt" "$OUT_DIR/icall.ll"; then
  echo "level 1 must not enable argument encryption" >&2
  exit 1
fi
if grep -Eq "call [^@]*@(add_bias|mix_double|sum_nine|ptr_mix|quad_shift)\(" "$OUT_DIR/icall.ll"; then
  echo "direct calls to icall targets must be rewritten at level 1" >&2
  exit 1
fi
# 全模块注册目标：add_bias、mix_double、sum_nine、ptr_mix、quad_shift。
REGISTER_CALLS=$(grep -Ec "call void @.*register_func" "$OUT_DIR/icall.ll")
if [ "$REGISTER_CALLS" -ne 5 ]; then
  echo "expected 5 deduplicated register_func calls, got $REGISTER_CALLS" >&2
  exit 1
fi

# —— level 2：参数加密（仅 add/sub/xor）+ 入口左值解密。
"$VLLVM_CLANG" "${TARGET_ARGS[@]}" -O0 -S -emit-llvm -fno-discard-value-names \
  -mllvm -vllvm-config=icall=2 \
  -DVLLVM_TEST_ICALL=1 "$SRC" -o "$OUT_DIR/icall_l2.ll"
grep -q "vllvm.icall.crypt" "$OUT_DIR/icall_l2.ll"
# decoy 诱饵下标机制已移除，任何等级都不得再生成。
if grep -Eq '__vllvm_icall.[0-9a-fA-F]+.decoy' "$OUT_DIR/icall_l2.ll"; then
  echo "decoy fake logic must stay removed" >&2
  exit 1
fi
# 加密常量在跳板汇编里经字面池读入（asm printer 把 =imm 物化为 Ltmp）。
"$VLLVM_CLANG" "${TARGET_ARGS[@]}" -O0 -S -mllvm -vllvm-config=icall=2 \
  -DVLLVM_TEST_ICALL=1 "$SRC" -o "$OUT_DIR/icall_l2.s"
grep -Eq "ldr[[:space:]]+x9, Ltmp" "$OUT_DIR/icall_l2.s"
for fn in add_bias sum_nine ptr_mix; do
  grep -Eq "define internal .*@__vllvm_icall\.[0-9A-F]+\.call\.[0-9]+\.$fn\(" \
    "$OUT_DIR/icall_l2.ll" || { echo "missing call trampoline for $fn" >&2; exit 1; }
  grep -Eq "call [^@]*@__vllvm_icall\.[0-9A-F]+\.call\.[0-9]+\.$fn\(" \
    "$OUT_DIR/icall_l2.ll" || { echo "call site must target its own trampoline: $fn" >&2; exit 1; }
done
# 入口解密左值：明文写回参数槽位 + volatile 读取。
grep -Eq "store i[0-9]+ %vllvm.icall.arg.plain" "$OUT_DIR/icall_l2.ll"
grep -q "load volatile" "$OUT_DIR/icall_l2.ll"
MIX_BODY=$(awk '/^define .*@mix_double\(/,/^\}/' "$OUT_DIR/icall_l2.ll")
if printf '%s\n' "$MIX_BODY" | grep -q "vllvm.icall.arg"; then
  echo "double-only args must not be encrypted" >&2
  exit 1
fi
# 调用者函数体不参与加密，也不允许直接调用注册目标；解密入口必须
# 出现在目标函数体内。
STRIPPED=$(awk 'BEGIN{keep=1}
  /^define/{keep = ($0 !~ /@__vllvm_icall\./)}
  keep{print}' "$OUT_DIR/icall_l2.ll")
if printf '%s\n' "$STRIPPED" | grep -q "vllvm.icall.arg.crypt"; then
  echo "call sites must forward plaintext args" >&2
  exit 1
fi
printf '%s\n' "$STRIPPED" | grep -q "vllvm.icall.arg.plain" || {
  echo "callee entry must decrypt args" >&2
  exit 1
}
if printf '%s\n' "$STRIPPED" | grep -Eq \
  "call [^@]*@(add_bias|mix_double|sum_nine|ptr_mix|quad_shift)\("; then
  echo "direct calls to icall targets must be rewritten" >&2
  exit 1
fi

if [ "$(uname -s)" = Darwin ]; then
  "$VLLVM_CLANG" -target aarch64-unknown-linux-gnu -O0 -ffreestanding \
    -DVLLVM_TEST_ICALL=1 -c "$SRC" -o "$OUT_DIR/icall-linux.o"
fi

case "$($VLLVM_CLANG -dumpmachine)" in
  arm64-*|aarch64-*)
    "$VLLVM_CLANG" "${LINK_ARGS[@]}" -O0 "$SRC" -o "$OUT_DIR/base"
    "$VLLVM_CLANG" "${LINK_ARGS[@]}" -O0 -DVLLVM_TEST_ICALL=1 \
      "$SRC" -o "$OUT_DIR/icall"
    "$VLLVM_CLANG" "${LINK_ARGS[@]}" -O0 -DVLLVM_TEST_ICALL=1 \
      -mllvm -vllvm-config=icall=2 "$SRC" -o "$OUT_DIR/icall_l2"
    "$OUT_DIR/base"
    "$OUT_DIR/icall"
    "$OUT_DIR/icall_l2"
    ;;
esac
