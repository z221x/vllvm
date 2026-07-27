#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
LLVM_BIN=${LLVM_BIN:-"$ROOT_DIR/build/llvm-macos/bin"}
OUT_DIR=${OUT_DIR:-"$ROOT_DIR/test/vmp/out"}
CXX=${CXX:-clang++}
mkdir -p "$OUT_DIR"

"$LLVM_BIN/llc" -march=vmp -mtriple=aarch64-unknown-linux-gnu -O0 \
  -filetype=obj "$ROOT_DIR/test/vmp/target_codegen.ll" \
  -o "$OUT_DIR/target_codegen.vmp"
"$LLVM_BIN/llc" -march=vmp -mtriple=aarch64-unknown-linux-gnu -O0 \
  -filetype=obj "$ROOT_DIR/test/vmp/target_hostcall.ll" \
  -o "$OUT_DIR/target_hostcall.vmp"

"$CXX" -std=c++20 -Wall -Wextra -Werror \
  -I"$ROOT_DIR/src/include" "$ROOT_DIR/test/vmp/target_stream_test.cpp" \
  -o "$OUT_DIR/target_stream_test"
"$OUT_DIR/target_stream_test" "$OUT_DIR/target_codegen.vmp" codegen
"$OUT_DIR/target_stream_test" "$OUT_DIR/target_hostcall.vmp" hostcall
