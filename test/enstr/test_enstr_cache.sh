#!/usr/bin/env bash
set -euo pipefail
ROOT=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
if [[ -z ${CLANG:-} ]]; then
  CLANG=$ROOT/build/llvm-macos/bin/clang
  [[ -x $CLANG ]] || CLANG=$ROOT/build/llvm-linux/bin/clang
fi
LLVM_AS=${LLVM_AS:-$(dirname "$CLANG")/llvm-as}
OUT_DIR=${OUT_DIR:-$ROOT/test/enstr/out/cache}
mkdir -p "$OUT_DIR"
args=(-Wno-override-module -Xclang -llvm-verify-each)
if [[ $(uname -s) == Darwin ]]; then args+=(-isysroot "$(xcrun --show-sdk-path)"); fi

for opt in 0 2; do
  ir="$OUT_DIR/O$opt.ll"
  "$CLANG" "${args[@]}" -O"$opt" -S -emit-llvm \
    "$ROOT/test/enstr/test_enstr_cache.ll" -o "$ir"
  "$LLVM_AS" "$ir" -o /dev/null
  [[ $(grep -Ec '^@vllvm\.enstr\.cache\..* = .*global ptr null' "$ir") == 2 ]]
  grep -q 'load atomic ptr.*acquire' "$ir"
  grep -q 'cmpxchg ptr.*acq_rel acquire' "$ir"
  grep -q 'store atomic ptr.*release' "$ir"
  grep -q 'call void @llvm.trap' "$ir"
  if grep -Eq 'cache-alpha|cache-beta' "$ir"; then
    echo 'plaintext string survived encryption' >&2; exit 1
  fi
  if grep -q '@free(' "$ir"; then
    echo 'cached strings must retain their static lifetime' >&2; exit 1
  fi

  # Count only generated allocations and intercept the OOM trap for testing.
  # Do not run VLLVM on transformed IR again or inherit intrinsic-only attributes.
  sed -e 's/@malloc(/@enstr_test_malloc(/g' \
    -e 's/@llvm.trap(/@enstr_test_trap(/g' \
    -e 's/^declare void @enstr_test_trap().*/declare void @enstr_test_trap()/' \
    "$ir" > "$OUT_DIR/counted_O$opt.ll"
  "$LLVM_AS" "$OUT_DIR/counted_O$opt.ll" -o /dev/null
  "$CLANG" "${args[@]}" -O"$opt" -Xclang -disable-llvm-passes -c \
    "$OUT_DIR/counted_O$opt.ll" -o "$OUT_DIR/O$opt.o"
  "$CLANG" "${args[@]}" -std=c11 -O2 -pthread \
    "$ROOT/test/enstr/test_enstr_cache_driver.c" "$OUT_DIR/O$opt.o" \
    -o "$OUT_DIR/O$opt"
  "$OUT_DIR/O$opt" single
  # Separate processes ensure every concurrent run starts with empty caches.
  for run in 1 2 3; do "$OUT_DIR/O$opt" threads; done
  "$OUT_DIR/O$opt" oom
done

# IR inputs need an explicit layout when the local LLVM lacks the X86 backend.
sed '1s/^/target datalayout = "e-p:32:32"\n/' \
  "$ROOT/test/enstr/test_enstr_cache.ll" > "$OUT_DIR/i386-input.ll"
# Cross-target verifier coverage also checks the target-sized malloc parameter.
for target in aarch64-linux-android23 i386-unknown-linux-gnu; do
  input="$ROOT/test/enstr/test_enstr_cache.ll"
  if [[ $target == i386-* ]]; then input="$OUT_DIR/i386-input.ll"; fi
  "$CLANG" --target="$target" -Wno-override-module -Xclang -llvm-verify-each \
    -O0 -S -emit-llvm "$input" \
    -o "$OUT_DIR/$target.ll"
  "$LLVM_AS" "$OUT_DIR/$target.ll" -o /dev/null
done
grep -q 'call ptr @malloc(i32 12)' "$OUT_DIR/i386-unknown-linux-gnu.ll"
echo 'PASS enstr cache: one allocation per string; stable pointers; cold-start races'
