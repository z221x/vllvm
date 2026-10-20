#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
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

mkdir -p "$OUT_DIR/multi_tu"

# 多 TU 复现：两个 TU 各自带 bcf 标注函数。seed 是模块级共享导出变量
# export_write，各 TU 必须以 weak 定义携带，链接器合并为全程序同一
# 变量——强定义会 duplicate symbol 链接失败（缺陷 #6 回归用例）。
cat > "$OUT_DIR/multi_tu/a.c" <<'EOF'
#if defined(VLLVM_TEST_BCF)
#define VLLVM_OBF __attribute__((annotate("vllvm:bcf")))
#else
#define VLLVM_OBF
#endif

VLLVM_OBF
int tu_a(int x) {
  int s = x;
  for (int i = 0; i < 9; ++i)
    s += i * x;
  return s;
}
EOF

cat > "$OUT_DIR/multi_tu/b.c" <<'EOF'
#if defined(VLLVM_TEST_BCF)
#define VLLVM_OBF __attribute__((annotate("vllvm:bcf")))
#else
#define VLLVM_OBF
#endif

#include <stdio.h>

extern int tu_a(int x);

VLLVM_OBF
int tu_b(int x) {
  int s = x;
  for (int i = 0; i < 7; ++i)
    s ^= i + x;
  return s;
}

int main(void) {
  printf("%d\n", tu_a(3) + tu_b(5));
  return 0;
}
EOF

"$VLLVM_CLANG" "${EXTRA_ARGS[@]}" -g0 -O0 "$OUT_DIR/multi_tu/a.c" \
  "$OUT_DIR/multi_tu/b.c" -o "$OUT_DIR/multi_tu/base"
"$OUT_DIR/multi_tu/base" > "$OUT_DIR/multi_tu/base.out"

for tu in a b; do
  "$VLLVM_CLANG" "${EXTRA_ARGS[@]}" -g0 -O0 -fno-discard-value-names \
    -DVLLVM_TEST_BCF=1 "$OUT_DIR/multi_tu/$tu.c" -S -emit-llvm \
    -o "$OUT_DIR/multi_tu/$tu.ll"
done

# 每个 TU 的 IR 各自带恰好一条 weak 定义（不带 internal/private 前缀）。
for tu in a b; do
  if [ "$(grep -Ec '^@export_write = weak global i16 0' "$OUT_DIR/multi_tu/$tu.ll")" -ne 1 ]; then
    echo "$tu.ll must define exactly one weak export_write" >&2
    exit 1
  fi
done

"$VLLVM_CLANG" "${EXTRA_ARGS[@]}" -g0 -O0 -DVLLVM_TEST_BCF=1 \
  "$OUT_DIR/multi_tu/a.c" "$OUT_DIR/multi_tu/b.c" -o "$OUT_DIR/multi_tu/obf"
"$OUT_DIR/multi_tu/obf" > "$OUT_DIR/multi_tu/obf.out"

diff -q "$OUT_DIR/multi_tu/base.out" "$OUT_DIR/multi_tu/obf.out" >/dev/null || {
  echo "multi-TU bcf output mismatch" >&2
  exit 1
}

# 链接后符号表恰一个 export_write：weak 合并生效、无重定义冲突。
COUNT=$(nm "$OUT_DIR/multi_tu/obf" | grep -c ' _export_write$' || true)
if [ "$COUNT" -ne 1 ]; then
  echo "expected exactly one merged export_write symbol, got $COUNT" >&2
  exit 1
fi

echo "bcf multi-TU: ok"
