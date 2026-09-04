#!/usr/bin/env bash
set -euo pipefail
# Invalid IR currently aborts the verifier; avoid large core dumps during regression.
ulimit -c 0
ROOT=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
OUT_DIR="${OUT_DIR:-$ROOT/test/android/out}"
LLVM_AS="${LLVM_AS:-$ROOT/build/llvm-macos/bin/llvm-as}"
[[ -x "$LLVM_AS" ]] || { echo 'Build the llvm-as target first' >&2; exit 1; }
failed=0
for mode in baseline ibr enstr fla icall lvars bcf vmfla vmp combined; do
  passed=0
  rejected=0
  ir_dir="$OUT_DIR/$mode/build/CMakeFiles/game.dir"
  mkdir -p "$OUT_DIR/$mode/verify"
  for ir in "$ir_dir"/*.ll; do
    [[ -f "$ir" ]] || continue
    if "$LLVM_AS" "$ir" -o /dev/null \
        >"$OUT_DIR/$mode/verify/$(basename "$ir").log" 2>&1; then
      passed=$((passed + 1))
    else
      rejected=$((rejected + 1))
    fi
  done
  echo "$mode: valid=$passed rejected=$rejected missing=$((24 - passed - rejected))"
  if [[ $passed != 24 || $rejected != 0 ]]; then failed=1; fi
done
exit "$failed"
