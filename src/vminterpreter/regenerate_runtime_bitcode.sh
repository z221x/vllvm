#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
ROOT_DIR=$(cd -- "$SCRIPT_DIR/../.." && pwd)
CLANGXX=${CLANGXX:-"$ROOT_DIR/build/llvm-macos/bin/clang++"}
OUTPUT="$SCRIPT_DIR/VmpRuntimeBitcode.inc"
MODE=${1:-generate}
TEMP_DIR=$(mktemp -d "${TMPDIR:-/tmp}/vllvm-vmp-runtime.XXXXXX")
trap 'rm -rf "$TEMP_DIR"' EXIT

EXTRA_ARGS=()
if [ "$(uname -s)" = Darwin ] && command -v xcrun >/dev/null 2>&1; then
  EXTRA_ARGS+=(-isysroot "$(xcrun --show-sdk-path)")
fi

(
  cd "$ROOT_DIR"
  "$CLANGXX" -std=c++17 -O2 -fno-exceptions -fno-rtti \
    -fno-threadsafe-statics -frandom-seed=vllvm-vmp-runtime -emit-llvm -c \
    -DVLLVM_VMP_AARCH64_RUNTIME=1 \
    src/vminterpreter/interpreter.cpp -Isrc/include \
    "${EXTRA_ARGS[@]}" -o "$TEMP_DIR/VmpRuntime.bc"
)
python3 "$SCRIPT_DIR/embed_bitcode.py" "$TEMP_DIR/VmpRuntime.bc" \
  "$TEMP_DIR/VmpRuntimeBitcode.inc"

if [ "$MODE" = "--check" ]; then
  cmp "$TEMP_DIR/VmpRuntimeBitcode.inc" "$OUTPUT"
elif [ "$MODE" = "generate" ]; then
  cp "$TEMP_DIR/VmpRuntimeBitcode.inc" "$OUTPUT"
else
  echo "usage: $0 [--check]" >&2
  exit 2
fi
