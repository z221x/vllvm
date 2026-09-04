#!/usr/bin/env bash
set -euo pipefail
ROOT=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
if [[ -z ${CLANG:-} ]]; then
  CLANG=$ROOT/build/llvm-macos/bin/clang
  [[ -x $CLANG ]] || CLANG=$ROOT/build/llvm-linux/bin/clang
fi
LLVM_AS=${LLVM_AS:-$(dirname "$CLANG")/llvm-as}
OUT_DIR=${OUT_DIR:-$ROOT/test/vmp/out/runtime-sdk}
mkdir -p "$OUT_DIR"
args=(-Wno-override-module -Xclang -llvm-verify-each -O0 -S -emit-llvm)
"$CLANG" "${args[@]}" --target=arm64-apple-macosx12.0 \
  "$ROOT/test/vmp/test_runtime_sdk.ll" -o "$OUT_DIR/macos.ll"
"$LLVM_AS" "$OUT_DIR/macos.ll" -o /dev/null
grep -q 'call i64 @__vllvm_vmp_execute' "$OUT_DIR/macos.ll"
grep -q '!"SDK Version", \[2 x i32\] \[i32 99, i32 1\]' "$OUT_DIR/macos.ll"

# Android modules must not acquire the SDK version used to build the runtime.
sed '/^!llvm.module.flags =/d' "$ROOT/test/vmp/test_runtime_sdk.ll" \
  > "$OUT_DIR/no-sdk.ll"
"$CLANG" "${args[@]}" --target=aarch64-linux-android23 \
  "$OUT_DIR/no-sdk.ll" -o "$OUT_DIR/android.ll"
"$LLVM_AS" "$OUT_DIR/android.ll" -o /dev/null
grep -q 'call i64 @__vllvm_vmp_execute' "$OUT_DIR/android.ll"
if grep -q '!"SDK Version"' "$OUT_DIR/android.ll"; then
  echo 'VMP runtime leaked its host SDK version into Android IR' >&2; exit 1
fi
echo 'PASS VMP runtime SDK: destination SDK retained; Android has no host SDK flag'
