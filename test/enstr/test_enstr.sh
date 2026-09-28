#!/usr/bin/env bash
set -euo pipefail
ROOT=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
if [[ -z ${CLANG:-} ]]; then
  CLANG=$ROOT/build/llvm-macos/bin/clang
  [[ -x $CLANG ]] || CLANG=$ROOT/build/llvm-linux/bin/clang
fi
OUT_DIR=${OUT_DIR:-$ROOT/test/enstr/out/basic}
mkdir -p "$OUT_DIR"
args=(-Xclang -llvm-verify-each)
if [[ $(uname -s) == Darwin ]]; then args+=(-isysroot "$(xcrun --show-sdk-path)"); fi
# Cover direct literals and both levels of global pointer indirection.
for opt in 0 2; do
  "$CLANG" "${args[@]}" -O"$opt" "$ROOT/test/enstr/test_enstr.c" \
    -o "$OUT_DIR/baseline_O$opt"
  "$CLANG" "${args[@]}" -O"$opt" -DVLLVM_TEST_ENSTR=1 \
    "$ROOT/test/enstr/test_enstr.c" "$ROOT/test/enstr/mmap_host_shim.c" \
    -o "$OUT_DIR/enstr_O$opt"
  "$OUT_DIR/baseline_O$opt" > "$OUT_DIR/baseline_O$opt.txt"
  "$OUT_DIR/enstr_O$opt" > "$OUT_DIR/enstr_O$opt.txt"
  cmp "$OUT_DIR/baseline_O$opt.txt" "$OUT_DIR/enstr_O$opt.txt"
  "$CLANG" "${args[@]}" -O"$opt" -DVLLVM_TEST_ENSTR=1 -S -emit-llvm \
    "$ROOT/test/enstr/test_enstr.c" -o "$OUT_DIR/enstr_O$opt.ll"
  if grep -q 'This is func' "$OUT_DIR/enstr_O$opt.ll"; then
    echo 'plaintext string survived encryption' >&2; exit 1
  fi
  # level 1（默认）：字符串进匿名池，常量保持原样。
  # 调用形态断言只在 O0 做：O2 的后续优化可能内联访问器。
  grep -q 'vllvm_enstr_pool_base' "$OUT_DIR/enstr_O$opt.ll"
  if [[ $opt == 0 ]]; then
    grep -Eq 'call ptr @__vllvm_enstr_get\(i64 [0-9]+\)' "$OUT_DIR/enstr_O$opt.ll"
  fi
  if grep -q 'vllvm.enstr.const.table' "$OUT_DIR/enstr_O$opt.ll"; then
    echo 'level 1 must not encrypt constants' >&2; exit 1
  fi

  # level 2：常量同样进密文表；明文立即数不再出现。
  "$CLANG" "${args[@]}" -O"$opt" -DVLLVM_TEST_ENSTR=1 -S -emit-llvm \
    -mllvm -vllvm-config=enstr=2 \
    "$ROOT/test/enstr/test_enstr.c" -o "$OUT_DIR/enstr_l2_O$opt.ll"
  if grep -q 'This is func' "$OUT_DIR/enstr_l2_O$opt.ll"; then
    echo 'plaintext string survived encryption' >&2; exit 1
  fi
  grep -q 'vllvm.enstr.const.table' "$OUT_DIR/enstr_l2_O$opt.ll"
  if grep -Eq '1956577150' "$OUT_DIR/enstr_l2_O$opt.ll"; then
    echo 'plaintext constant survived level 2 encryption' >&2; exit 1
  fi
  "$CLANG" "${args[@]}" -O"$opt" -DVLLVM_TEST_ENSTR=1 \
    -mllvm -vllvm-config=enstr=2 \
    "$ROOT/test/enstr/test_enstr.c" "$ROOT/test/enstr/mmap_host_shim.c" \
    -o "$OUT_DIR/enstr_l2_O$opt"
  "$OUT_DIR/enstr_l2_O$opt" > "$OUT_DIR/enstr_l2_O$opt.txt"
  cmp "$OUT_DIR/baseline_O$opt.txt" "$OUT_DIR/enstr_l2_O$opt.txt"
done
echo 'PASS enstr: O0/O2 IR verification and runtime output'
