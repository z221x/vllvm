#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
OUT_DIR=${OUT_DIR:-"$ROOT_DIR/test/vmp/out"}
CXX=${CXX:-clang++}

mkdir -p "$OUT_DIR"
"$CXX" -std=c++20 -Wall -Wextra -Werror -DVLLVM_VMP_TESTING=1 \
  "$ROOT_DIR/src/vminterpreter/interpreter.cpp" \
  "$ROOT_DIR/test/vmp/interpreter_test.cpp" \
  -o "$OUT_DIR/interpreter_test"
"$OUT_DIR/interpreter_test"
