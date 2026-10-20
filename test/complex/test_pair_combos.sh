#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
OUT_DIR="${OUT_DIR:-$ROOT_DIR/test/complex/out/pairs}"

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

# 逐对组合冒烟：每个 pass 两两叠加到同一批函数上，编译 + 运行 + 退出码
# 与 base 一致即过。fla 是 vmfla 的别名（链路同一），不单独入池。
# 覆盖点：switch（ibr）、字符串常量（enstr）、跨函数调用（icall）、
# 循环/PHI（bcf/vmfla/bb2func/merge）、小函数整体（vmp 虚拟化或回退）。
cat > "$OUT_DIR/pair_fixture.c" <<'EOF'
#if defined(VLLVM_PAIR)
#define OBF __attribute__((annotate(VLLVM_PAIR)))
#else
#define OBF
#endif

static const char *pair_tag = "vllvm-pair-smoke";

OBF static int mix(int x) {
  int s = x;
  for (int i = 0; i < 12; ++i) {
    switch ((s ^ i) & 3) {
    case 0: s += i * 3 + pair_tag[0]; break;
    case 1: s -= i + x; break;
    case 2: s ^= x << 1; break;
    default: s += (s & 7) - i;
    }
  }
  return s;
}

OBF static int fold(int a, int b) {
  int t = 0;
  for (int i = 0; i < 9; ++i)
    t += (i & 1) ? (a ^ i) : (b + i);
  return t;
}

OBF int chain(int x) {
  int r = 0;
  for (int i = 0; i < 7; ++i)
    r += mix(x + i) - fold(x, i);
  return r;
}

OBF int main(void) {
  int acc = 0;
  for (int i = 0; i < 21; ++i)
    acc = acc * 3 + chain(i * 5 + 1) + ((i & 2) ? mix(i) : fold(i, acc & 15));
  return (acc ^ (acc >> 8)) & 255;
}
EOF

"$VLLVM_CLANG" "${EXTRA_ARGS[@]}" -g0 -O0 "$OUT_DIR/pair_fixture.c" \
  -o "$OUT_DIR/pair_base"
set +e
"$OUT_DIR/pair_base"
BASE_RC=$?
set -e

PASSES=(bcf ibr icall vmfla enstr bb2func merge vmp)
# ibr+vmfla 曾为缺陷 #3（旧调度反序执行：ibr 先改写、vmfla 再平坦化，
# 静默错结果）；现已改为 vmfla 先行、ibr 延迟到晚期分发作用于平坦化
# 产物（见 PassInfo.cpp IbrLateAttr），本逐对冒烟保持全量覆盖防回归。
TOTAL=0
FAILED=0
for ((i = 0; i < ${#PASSES[@]}; ++i)); do
  for ((j = i + 1; j < ${#PASSES[@]}; ++j)); do
    P1=${PASSES[i]}
    P2=${PASSES[j]}
    PAIR="vllvm:$P1,vllvm:$P2"
    TOTAL=$((TOTAL + 1))
    if "$VLLVM_CLANG" "${EXTRA_ARGS[@]}" -g0 -O0 \
        -DVLLVM_PAIR="\"$PAIR\"" "$OUT_DIR/pair_fixture.c" \
        -o "$OUT_DIR/pair_bin" >"$OUT_DIR/pair_build.log" 2>&1; then
      set +e
      "$OUT_DIR/pair_bin"
      RC=$?
      set -e
      if [ "$RC" -ne "$BASE_RC" ]; then
        FAILED=$((FAILED + 1))
        echo "FAIL $P1+$P2: exit $RC != base $BASE_RC"
      fi
    else
      FAILED=$((FAILED + 1))
      echo "FAIL $P1+$P2: build error (pair_build.log)"
      tail -3 "$OUT_DIR/pair_build.log" | sed 's/^/    /'
    fi
  done
done

if [ "$FAILED" -ne 0 ]; then
  echo "pair combos: $FAILED/$TOTAL failed" >&2
  exit 1
fi
echo "pair combos: $TOTAL/$TOTAL ok (base exit $BASE_RC)"
