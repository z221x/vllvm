#!/usr/bin/env bash
set -euo pipefail
ulimit -c 0

ROOT_DIR=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
SRC_DIR="$ROOT_DIR/test/indirectcall"
OUT_DIR="${OUT_DIR:-$ROOT_DIR/test/indirectcall/out/cross_tu}"

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
if [ "$(uname -s)" = Darwin ] && command -v xcrun >/dev/null 2>&1; then
  SDK_PATH=$(xcrun --show-sdk-path 2>/dev/null || true)
  if [ -n "$SDK_PATH" ]; then
    EXTRA_ARGS+=(-isysroot "$SDK_PATH")
  fi
fi

mkdir -p "$OUT_DIR"

# 提取单个函数的 IR 文本（define 到下一个顶层定义之前）。
extract_fn() {
  awk -v fn="@$2" '
    /^(define|declare)/ { keep = index($0, fn) > 0 }
    keep { print }
  ' "$1"
}

# 判断函数是否带 vllvm.icall.crypt 属性：字符串属性可能内联在 define
# 行，也可能折叠进末尾的属性组（#N），两种形式都要识别。
fn_has_crypt() {
  local file=$1 fn=$2 hdr groups g
  hdr=$(awk -v fn="@$fn" '
    /^define/ { if (index($0, fn) > 0) { print; exit } }
  ' "$file")
  [ -n "$hdr" ] || return 1
  printf '%s' "$hdr" | grep -q '"vllvm.icall.crypt"' && return 0
  groups=$(printf '%s' "$hdr" | grep -oE '#[0-9]+' | tr -d '#')
  for g in $groups; do
    grep -qE "^attributes #$g = \\{[^}]*vllvm.icall.crypt" "$file" && return 0
  done
  return 1
}

for opt in 0; do
  # 两个 TU 分开编译再链接：被调方 TU 带标注，调用方 TU 保持明文直调。
  "$VLLVM_CLANG" "${EXTRA_ARGS[@]}" -O$opt -g0 -S -emit-llvm \
    -fno-discard-value-names -mllvm -vllvm-config=icall=2 \
    -DVLLVM_TEST_ICALL=1 "$SRC_DIR/icall_cross_tu_callee.c" \
    -o "$OUT_DIR/callee_O$opt.ll"
  "$VLLVM_CLANG" "${EXTRA_ARGS[@]}" -O$opt -g0 \
    -mllvm -vllvm-config=icall=2 \
    -DVLLVM_TEST_ICALL=1 -c "$SRC_DIR/icall_cross_tu_callee.c" \
    -o "$OUT_DIR/callee_O$opt.o"
  "$VLLVM_CLANG" "${EXTRA_ARGS[@]}" -O$opt -g0 \
    -c "$SRC_DIR/icall_cross_tu_caller.c" -o "$OUT_DIR/caller_O$opt.o"
  "$VLLVM_CLANG" "${EXTRA_ARGS[@]}" -O$opt -g0 \
    "$OUT_DIR/caller_O$opt.o" "$OUT_DIR/callee_O$opt.o" \
    -o "$OUT_DIR/cross_tu_O$opt"

  # 明文参照
  "$VLLVM_CLANG" "${EXTRA_ARGS[@]}" -O$opt -g0 \
    "$SRC_DIR/icall_cross_tu_caller.c" "$SRC_DIR/icall_cross_tu_callee.c" \
    -o "$OUT_DIR/base_O$opt"

  set +e
  base_out=$("$OUT_DIR/base_O$opt")
  base_status=$?
  icall_out=$("$OUT_DIR/cross_tu_O$opt")
  icall_status=$?
  set -e

  if [ "$base_status" -ne 0 ] || [ "$icall_status" -ne 0 ] ||
     [ "$base_out" != "$icall_out" ]; then
    echo "O$opt cross-TU mismatch: base='$base_out'($base_status) icall='$icall_out'($icall_status)" >&2
    exit 1
  fi
  if [ "$icall_out" != "cross-tu OK" ]; then
    echo "O$opt cross-TU run reported wrong result: '$icall_out'" >&2
    exit 1
  fi
done

CALLEE_LL="$OUT_DIR/callee_O0.ll"

# 被调方 TU 正常入池。
grep -q "__vllvm_icall" "$CALLEE_LL"
grep -Eq "call void @__vllvm_icall\..*\.register_func" "$CALLEE_LL"

# 外部链接目标不做参数加密。
if fn_has_crypt "$CALLEE_LL" cross_tu_target; then
  echo "externally-visible icall target must not encrypt args" >&2
  exit 1
fi

# 内部链接目标照常加密。
fn_has_crypt "$CALLEE_LL" internal_target || {
  echo "internal icall target should still encrypt args" >&2
  exit 1
}

# 加密位置：调用点普通直调专属 naked 跳板，加密链与 ret x19 查表都在
# 跳板的汇编里，调用点函数体保持明文。
grep -Eq "define internal .*@__vllvm_icall\.[0-9A-F]+\.call\.[0-9]+\.internal_target\(" \
  "$CALLEE_LL" || { echo "missing call trampoline for internal target" >&2; exit 1; }
grep -q "ret x19" "$CALLEE_LL"
grep -Eq "i32 nest [0-9]+" "$CALLEE_LL"
extract_fn "$CALLEE_LL" cross_tu_entry | \
  grep -Eq "call icallcc [^@]*@__vllvm_icall\.[0-9A-F]+\.call\.[0-9]+\."
if extract_fn "$CALLEE_LL" cross_tu_entry | \
  grep -q "vllvm.icall.arg.crypt"; then
  echo "call site must stay a plaintext trampoline call" >&2
  exit 1
fi
# 解密入口在被调函数体内：明文写回参数槽位，后续按左值（volatile）读取。
extract_fn "$CALLEE_LL" internal_target | grep -q "vllvm.icall.arg.plain"
extract_fn "$CALLEE_LL" internal_target | grep -q "load volatile"

# 调用方 TU 不带标注：保持明文直调，不建池。
"$VLLVM_CLANG" "${EXTRA_ARGS[@]}" -O0 -g0 -S -emit-llvm \
  -fno-discard-value-names \
  "$SRC_DIR/icall_cross_tu_caller.c" -o "$OUT_DIR/caller_O0.ll"
if grep -q "__vllvm_icall" "$OUT_DIR/caller_O0.ll"; then
  echo "unannotated caller TU must not build an icall pool" >&2
  exit 1
fi
grep -q "call i64 @cross_tu_target" "$OUT_DIR/caller_O0.ll"

echo "PASS icall cross-TU: external targets pooled without arg encryption; internal targets encrypted"
