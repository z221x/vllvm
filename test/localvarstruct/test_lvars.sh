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

EXTRA_ARGS=()
if [ "$(uname -s)" = "Darwin" ] && command -v xcrun >/dev/null 2>&1; then
  SDK_PATH=$(xcrun --show-sdk-path 2>/dev/null || true)
  if [ -n "$SDK_PATH" ]; then
    EXTRA_ARGS+=(-isysroot "$SDK_PATH")
  fi
fi

NO_DEBUG_ARGS=(-g0)

strip_binary() {
  local bin=$1
  if command -v strip >/dev/null 2>&1; then
    strip "$bin" >/dev/null 2>&1 || strip -x "$bin" >/dev/null 2>&1 || true
  fi
}

mkdir -p "$OUT_DIR"

# lvars 标注现在别名到 vmfla：参数（局部变量）结构化只能捆绑在 vmfla 中
# 执行，不再提供独立的结构化 pass。本脚本验证别名语义与捆绑产物。
"$VLLVM_CLANG" "${EXTRA_ARGS[@]}" "${NO_DEBUG_ARGS[@]}" -O0 -S \
  -emit-llvm -DVLLVM_TEST_LVARS=1 "$SRC" \
  -o "$OUT_DIR/test_lvars.ll"

# 结构化由 vmfla 内部计划承担：只允许一张 vmfla 常量表，独立
# localvars 偏移表不再存在。
grep -q "vllvm.vmfla.const.table.* global " "$OUT_DIR/test_lvars.ll"
if grep -q "vllvm.localvars.table" "$OUT_DIR/test_lvars.ll"; then
  echo "lvars alias must not emit a standalone localvars table" >&2
  exit 1
fi

# 局部变量偏移经共享表解密：volatile 读取 + 表内 key 异或，无明文 key。
grep -q "load volatile" "$OUT_DIR/test_lvars.ll"
grep -Eq "xor i32 %[0-9]+, %[0-9]+" "$OUT_DIR/test_lvars.ll"
if grep -Eq "xor i32 %[0-9]+, [-0-9]+" "$OUT_DIR/test_lvars.ll"; then
  echo "offset keys must be loaded from the shared table" >&2
  exit 1
fi

# 结构化结构体来自堆分配，退出路径释放。flatten 阶段自身会创建合法的
# reg2mem 栈槽，因此这里不做模块级"无 alloca"断言。
grep -q "call.*@malloc" "$OUT_DIR/test_lvars.ll"
grep -q "call.*@free" "$OUT_DIR/test_lvars.ll"
# 结构化结构体类型名是结构化生效的直接证据。
grep -q "vllvm.localvars\." "$OUT_DIR/test_lvars.ll"

# 常量表改为参数传递：impl 带表参数，wrapper 经守卫伪调用点传表；
# 表全局不允许再被解密访问模式直接引用。
grep -Eq 'define private [^@]*@[A-Za-z0-9_]+\.vllvm\.impl\([^)]*ptr %' \
  "$OUT_DIR/test_lvars.ll"
grep -Eq 'call[^@]*@[A-Za-z0-9_]+\.vllvm\.impl\([^)]*@vllvm\.vmfla\.const\.table' \
  "$OUT_DIR/test_lvars.ll"
if grep -Eq '(getelementptr \[[0-9]+ x i32\], ptr|load volatile i32, ptr) @vllvm\.vmfla\.const\.table' \
  "$OUT_DIR/test_lvars.ll"; then
  echo "vmfla constant table must be passed as a parameter, not GEP-referenced" >&2
  exit 1
fi

# 防传播伪调用点：volatile 守卫全局 + 偏移表指针的第二调用点必须存在
# （伪调用点块名不保留在输出 IR 中，用偏移 GEP 特征断言）。
grep -q "vllvm.tablesink" "$OUT_DIR/test_lvars.ll"
grep -Eq 'getelementptr .i8, ptr @vllvm\.[A-Za-z0-9_.]+, i64' \
  "$OUT_DIR/test_lvars.ll"

"$VLLVM_CLANG" "${EXTRA_ARGS[@]}" "${NO_DEBUG_ARGS[@]}" -O0 "$SRC" \
  -o "$OUT_DIR/test_lvars_base"
"$VLLVM_CLANG" "${EXTRA_ARGS[@]}" "${NO_DEBUG_ARGS[@]}" -O0 \
  -DVLLVM_TEST_LVARS=1 "$SRC" \
  -o "$OUT_DIR/test_lvars"
strip_binary "$OUT_DIR/test_lvars_base"
strip_binary "$OUT_DIR/test_lvars"

set +e
"$OUT_DIR/test_lvars_base"
BASE_STATUS=$?
"$OUT_DIR/test_lvars"
LVARS_STATUS=$?
set -e

if [ "$BASE_STATUS" -ne "$LVARS_STATUS" ]; then
  echo "exit status mismatch: base=$BASE_STATUS lvars=$LVARS_STATUS" >&2
  exit 1
fi

echo "test_lvars passed"
