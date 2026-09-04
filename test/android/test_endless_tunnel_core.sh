#!/usr/bin/env bash
set -euo pipefail
ROOT=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
OUT_DIR="${OUT_DIR:-$ROOT/test/android/out}"
: "${ANDROID_SERIAL:?Set ANDROID_SERIAL to the intended test device}"
REMOTE=/data/local/tmp/vllvm-endless-tunnel-988d73b
adb -s "$ANDROID_SERIAL" shell mkdir -p "$REMOTE"
failed=0
for mode in baseline ibr enstr fla icall lvars bcf vmfla vmp combined; do
  if [[ ! -x "$OUT_DIR/core/$mode" ]]; then
    echo "FAIL $mode: build the core executable first"
    failed=1
    continue
  fi
  adb -s "$ANDROID_SERIAL" push "$OUT_DIR/core/$mode" "$REMOTE/$mode" >/dev/null
  if ! adb -s "$ANDROID_SERIAL" shell timeout 20 "$REMOTE/$mode" \
      >"$OUT_DIR/core/$mode.run.log" 2>&1; then
    echo "FAIL $mode: execution failed or timed out"
    failed=1
  elif ! grep -Eq '^cases=57600 checksum=[0-9a-f]{16}' "$OUT_DIR/core/$mode.run.log"; then
    echo "FAIL $mode: missing test completion marker"
    failed=1
  elif ! cmp -s "$OUT_DIR/core/baseline.run.log" "$OUT_DIR/core/$mode.run.log"; then
    echo "FAIL $mode: output differs from baseline"
    failed=1
  else
    echo "PASS $mode: identical output"
  fi
done
exit "$failed"
