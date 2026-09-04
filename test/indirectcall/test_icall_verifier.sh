#!/usr/bin/env bash
set -euo pipefail
ulimit -c 0
ROOT=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
if [[ -z ${LLVM_AS:-} ]]; then
  LLVM_AS=$ROOT/build/llvm-macos/bin/llvm-as
  [[ -x $LLVM_AS ]] || LLVM_AS=$ROOT/build/llvm-linux/bin/llvm-as
fi
OUT_DIR=${OUT_DIR:-$ROOT/test/indirectcall/out/verifier}
mkdir -p "$OUT_DIR"
"$LLVM_AS" "$ROOT/test/indirectcall/test_icall_verifier.ll" -o /dev/null

# Permit only the custom i32 index; keep ordinary nest and other ABI checks strict.
reject() {
  local name=$1 ir=$2 reason=$3
  if printf '%s\n' "$ir" | "$LLVM_AS" -o /dev/null >"$OUT_DIR/$name.log" 2>&1; then
    echo "unexpected verifier acceptance: $name" >&2; exit 1
  fi
  grep -q "$reason" "$OUT_DIR/$name.log"
}
reject normal_i32 'declare void @bad(i32 nest)' 'incompatible type'
reject index_i64 'declare icallcc void @bad(i64 nest)' 'must be i32'
reject index_ptr 'declare icallcc void @bad(ptr nest)' 'must be i32'
reject missing 'declare icallcc void @bad(i32)' 'exactly one nest i32'
reject duplicate 'declare icallcc void @bad(i32 nest, i32 nest)' 'More than one parameter'
reject varargs 'declare icallcc void @bad(i32 nest, ...)' 'does not support varargs'
reject other_attr 'declare icallcc void @bad(i32 nest nonnull)' 'incompatible type'
reject call_i64 'declare icallcc void @target(i32 nest)
define void @caller() {
  call icallcc void @target(i64 nest 0)
  ret void
}' 'must be i32'
reject call_missing 'declare icallcc void @target(i32 nest)
define void @caller() {
  call icallcc void @target(i32 0)
  ret void
}' 'exactly one nest i32'
echo 'PASS icall verifier: valid ABI accepted; 9 invalid cases rejected'
