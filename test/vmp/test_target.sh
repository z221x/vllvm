#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
LLVM_BIN=${LLVM_BIN:-"$ROOT_DIR/build/llvm-macos/bin"}
OUT_DIR=${OUT_DIR:-"$ROOT_DIR/test/vmp/out"}
mkdir -p "$OUT_DIR"

"$LLVM_BIN/llc" -march=vmp -mtriple=bpfel -O0 -verify-machineinstrs \
  -filetype=obj "$ROOT_DIR/test/vmp/target_codegen.ll" \
  -o "$OUT_DIR/target_codegen.o"
"$LLVM_BIN/llvm-readobj" --sections "$OUT_DIR/target_codegen.o" \
  >"$OUT_DIR/target_codegen.sections"

grep -q '\.vmp\.code' "$OUT_DIR/target_codegen.sections"
grep -q '\.vmp\.const' "$OUT_DIR/target_codegen.sections"
grep -q '\.vmp\.meta' "$OUT_DIR/target_codegen.sections"

"$LLVM_BIN/llc" -march=vmp -mtriple=bpfel -O0 -verify-machineinstrs \
  -filetype=obj "$ROOT_DIR/test/vmp/target_hostcall.ll" \
  -o "$OUT_DIR/target_hostcall.o"
"$LLVM_BIN/llvm-readobj" --relocations "$OUT_DIR/target_hostcall.o" \
  >"$OUT_DIR/target_hostcall.relocations"
grep -q 'R_BPF_64_32 __vllvm_vmp_hostcall.0' \
  "$OUT_DIR/target_hostcall.relocations"
