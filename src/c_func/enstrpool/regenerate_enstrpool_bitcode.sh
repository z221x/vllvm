#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
ROOT_DIR=$(cd -- "$SCRIPT_DIR/../../.." && pwd)
CLANG=${CLANG:-"$ROOT_DIR/build/llvm-macos/bin/clang"}
OUTPUT="$SCRIPT_DIR/EnstrPoolBitcode.inc"
MODE=${1:-generate}
TEMP_DIR=$(mktemp -d "${TMPDIR:-/tmp}/vllvm-enstr-pool.XXXXXX")
trap 'rm -rf "$TEMP_DIR"' EXIT

# Android/ELF 优先：C 源只依赖 mmap 符号，位码按 LP64 布局生成后由
# pass 重定目标到当前模块的 triple。
(
  cd "$ROOT_DIR"
  "$CLANG" -std=c11 -O2 -fno-threadsafe-statics -frandom-seed=vllvm-enstr-pool \
    -emit-llvm -c src/c_func/enstrpool/enstrpool.c \
    -o "$TEMP_DIR/enstrpool.bc"
)
python3 "$SCRIPT_DIR/../vminterpreter/embed_bitcode.py" \
  "$TEMP_DIR/enstrpool.bc" "$TEMP_DIR/EnstrPoolBitcode.inc" EnstrPoolBitcode

if [ "$MODE" = "--check" ]; then
  cmp "$TEMP_DIR/EnstrPoolBitcode.inc" "$OUTPUT"
elif [ "$MODE" = "generate" ]; then
  cp "$TEMP_DIR/EnstrPoolBitcode.inc" "$OUTPUT"
else
  echo "usage: $0 [--check]" >&2
  exit 2
fi
