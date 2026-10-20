#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
OUT_DIR="${OUT_DIR:-$ROOT_DIR/test/vmp/out}"

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

mkdir -p "$OUT_DIR/static_dup"

# 缺陷 #7 回归：vmp 安装 wrapper 时 deleteBody 会把 linkage 重置为
# External，静态（internal）函数被虚拟化后以外部符号参与链接，与其他
# TU 的同名函数 duplicate symbol。wrapper 必须保持原函数 linkage。
cat > "$OUT_DIR/static_dup/a.c" <<'EOF'
#if defined(VLLVM_TEST_VMP)
#define VLLVM_OBF __attribute__((annotate("vllvm:vmp")))
#else
#define VLLVM_OBF
#endif

VLLVM_OBF
static int shadow(int x) {
  int s = x;
  for (int i = 0; i < 5; ++i)
    s += i;
  return s;
}

int outer_a(int x) {
  return shadow(x) * 2;
}
EOF

cat > "$OUT_DIR/static_dup/b.c" <<'EOF'
#include <stdio.h>

extern int outer_a(int x);

int shadow(int x) {
  return x - 1;
}

int main(void) {
  printf("%d\n", outer_a(3) + shadow(5));
  return 0;
}
EOF

"$VLLVM_CLANG" "${EXTRA_ARGS[@]}" -g0 -O0 "$OUT_DIR/static_dup/a.c" \
  "$OUT_DIR/static_dup/b.c" -o "$OUT_DIR/static_dup/base"
"$OUT_DIR/static_dup/base" > "$OUT_DIR/static_dup/base.out"

# 静态函数必须被真实虚拟化（非 fallback），且 wrapper 保持 internal。
"$VLLVM_CLANG" "${EXTRA_ARGS[@]}" -g0 -O0 -DVLLVM_TEST_VMP=1 \
  "$OUT_DIR/static_dup/a.c" -S -emit-llvm -o "$OUT_DIR/static_dup/a.ll" 2>/dev/null
grep -q "__vllvm_vmp_code.shadow" "$OUT_DIR/static_dup/a.ll" || {
  echo "static shadow must be virtualized, not fallback" >&2
  exit 1
}
grep -Eq '^define internal [^@]*@shadow' "$OUT_DIR/static_dup/a.ll" || {
  echo "virtualized static function must keep internal linkage" >&2
  exit 1
}

# 与外部同名函数链接不允许 duplicate symbol，输出与基线一致。
"$VLLVM_CLANG" "${EXTRA_ARGS[@]}" -g0 -O0 -DVLLVM_TEST_VMP=1 \
  "$OUT_DIR/static_dup/a.c" "$OUT_DIR/static_dup/b.c" \
  -o "$OUT_DIR/static_dup/obf"
"$OUT_DIR/static_dup/obf" > "$OUT_DIR/static_dup/obf.out"
diff -q "$OUT_DIR/static_dup/base.out" "$OUT_DIR/static_dup/obf.out" >/dev/null || {
  echo "vmp static-dup output mismatch" >&2
  exit 1
}

echo "vmp static-dup: ok"
