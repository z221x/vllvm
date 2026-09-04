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
    "$ROOT/test/enstr/test_enstr.c" -o "$OUT_DIR/enstr_O$opt"
  "$OUT_DIR/baseline_O$opt" > "$OUT_DIR/baseline_O$opt.txt"
  "$OUT_DIR/enstr_O$opt" > "$OUT_DIR/enstr_O$opt.txt"
  cmp "$OUT_DIR/baseline_O$opt.txt" "$OUT_DIR/enstr_O$opt.txt"
  "$CLANG" "${args[@]}" -O"$opt" -DVLLVM_TEST_ENSTR=1 -S -emit-llvm \
    "$ROOT/test/enstr/test_enstr.c" -o "$OUT_DIR/enstr_O$opt.ll"
  if grep -q 'This is func' "$OUT_DIR/enstr_O$opt.ll"; then
    echo 'plaintext string survived encryption' >&2; exit 1
  fi
done
echo 'PASS enstr: O0/O2 IR verification and runtime output'
