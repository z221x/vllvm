#!/usr/bin/env bash
set -euo pipefail
ROOT=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
if [[ -z ${CLANG:-} ]]; then
  CLANG=$ROOT/build/llvm-macos/bin/clang
  [[ -x $CLANG ]] || CLANG=$ROOT/build/llvm-linux/bin/clang
fi
LLVM_AS=${LLVM_AS:-$(dirname "$CLANG")/llvm-as}
OUT_DIR=${OUT_DIR:-$ROOT/test/enstr/out/phi}
mkdir -p "$OUT_DIR"
args=(-Wno-override-module -Xclang -llvm-verify-each)
if [[ $(uname -s) == Darwin ]]; then args+=(-isysroot "$(xcrun --show-sdk-path)"); fi
for opt in 0 2; do
  "$CLANG" "${args[@]}" -O"$opt" -S -emit-llvm \
    "$ROOT/test/enstr/test_enstr_phi.ll" -o "$OUT_DIR/O$opt.ll"
  "$LLVM_AS" "$OUT_DIR/O$opt.ll" -o /dev/null
  # O2 may inline the decryptor; O0 verifies that it was actually generated.
  if [[ $opt == 0 ]]; then
    grep -q '_decrypto' "$OUT_DIR/O$opt.ll"
    grep -q 'reg2mem' "$OUT_DIR/O$opt.ll"
    # Later LCSSA may introduce fresh PHIs; the original PHIs must be gone.
    if grep -Eq '%(text|slot|unused|i) = phi ' "$OUT_DIR/O$opt.ll"; then
      echo 'enstr must lower PHIs before collecting string users' >&2; exit 1
    fi
  fi
  if grep -Eq 'c"(left|right)\\00"' "$OUT_DIR/O$opt.ll"; then
    echo "plaintext string survived encryption" >&2; exit 1
  fi
  "$CLANG" "${args[@]}" -O"$opt" "$ROOT/test/enstr/test_enstr_phi.ll" \
    "$ROOT/test/enstr/test_enstr_phi_driver.c" -o "$OUT_DIR/O$opt"
  "$OUT_DIR/O$opt"
done
echo 'PASS enstr PHI: O0/O2, direct/pointer strings, duplicate edges, loop'
