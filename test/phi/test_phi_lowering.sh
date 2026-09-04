#!/usr/bin/env bash
set -euo pipefail
ROOT=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
if [[ -z ${CLANG:-} ]]; then
  CLANG=$ROOT/build/llvm-macos/bin/clang
  [[ -x $CLANG ]] || CLANG=$ROOT/build/llvm-linux/bin/clang
fi
CLANGXX=${CLANGXX:-$(dirname "$CLANG")/clang++}
LLVM_AS=${LLVM_AS:-$(dirname "$CLANG")/llvm-as}
OUT_DIR=${OUT_DIR:-$ROOT/test/phi/out}
mkdir -p "$OUT_DIR"
args=(-Wno-override-module -Xclang -llvm-verify-each)
if [[ $(uname -s) == Darwin ]]; then args+=(-isysroot "$(xcrun --show-sdk-path)"); fi

for mode in baseline enstr fla vmfla enstr_fla enstr_vmfla; do
  attrs=""
  case "$mode" in
    enstr|fla|vmfla) attrs="\"vllvm.$mode\"" ;;
    enstr_fla) attrs='"vllvm.enstr" "vllvm.fla"' ;;
    enstr_vmfla) attrs='"vllvm.enstr" "vllvm.vmfla"' ;;
  esac
  sed "s/attributes #0 = { noinline }/attributes #0 = { noinline $attrs }/" \
    "$ROOT/test/phi/test_phi_lowering.ll" > "$OUT_DIR/$mode.input.ll"
  for opt in 0 2; do
    ir="$OUT_DIR/${mode}_O$opt.ll"
    emit_args=("${args[@]}")
    if [[ $mode != baseline && $opt == 0 ]]; then
      emit_args+=(-mllvm -print-after-all)
    fi
    "$CLANG" "${emit_args[@]}" -O"$opt" -S -emit-llvm \
      "$OUT_DIR/$mode.input.ll" -o "$ir" 2> "$OUT_DIR/${mode}_O$opt.log"
    "$LLVM_AS" "$ir" -o /dev/null
    if [[ $mode != baseline && $opt == 0 ]]; then
      # Verify the contract immediately after each consumer, not after later
      # LCSSA/mem2reg optimizations, which are allowed to form new PHIs.
      awk '/^; \*\*\* IR Dump After/ {
        show = /VLLVMEncryptoStrDispatchPass|VLLVMFunctionDispatchPass/
      }
      show && !/^\[vllvm\]/ { print }' "$OUT_DIR/${mode}_O$opt.log" \
        > "$OUT_DIR/$mode.after-lowering.log"
      # The enstr dispatch is a no-op for fla/vmfla-only input.
      if [[ $mode == fla || $mode == vmfla ]]; then
        awk '/^; \*\*\* IR Dump After/ { show = /VLLVMFunctionDispatchPass/ }
          show && !/^\[vllvm\]/ { print }' "$OUT_DIR/${mode}_O$opt.log" \
          > "$OUT_DIR/$mode.after-lowering.log"
      fi
      grep -q '^define ' "$OUT_DIR/$mode.after-lowering.log"
      if grep -Eq ' = phi ' "$OUT_DIR/$mode.after-lowering.log"; then
        echo "$mode did not lower all supported PHIs" >&2; exit 1
      fi
      grep -q 'invoke.phi.edge' "$ir"
      grep -q '!DISubprogram' "$ir"
    fi
    case "$mode" in
      *enstr*)
        grep -q 'vllvm.enstr.cache' "$ir"
        if grep -Eq 'c"(left|right)\\00"' "$ir"; then
          echo "$mode left a plaintext string" >&2; exit 1
        fi ;;
    esac
    if [[ $mode == fla || $mode == enstr_fla ]]; then
      grep -q 'vllvm.fla.const.table' "$ir"
    elif [[ $mode == *vmfla ]]; then
      grep -q 'vllvm.vmfla.const.table' "$ir"
    fi
    # Compile the original fixture, never feed transformed IR through VLLVM twice.
    "$CLANGXX" "${args[@]}" -O"$opt" "$OUT_DIR/$mode.input.ll" \
      "$ROOT/test/phi/test_phi_lowering_driver.cpp" -o "$OUT_DIR/${mode}_O$opt"
    "$OUT_DIR/${mode}_O$opt"
    echo "PASS $mode O$opt"
  done
done

ir="$OUT_DIR/funclet-enstr-supported.ll"
"$CLANG" --target=aarch64-pc-windows-msvc -Wno-override-module \
  -Xclang -llvm-verify-each -O0 -S -emit-llvm \
  "$ROOT/test/phi/test_phi_funclet_enstr.ll" -o "$ir"
"$LLVM_AS" "$ir" -o /dev/null
test "$(grep -c 'call ptr @_decrypto.*\[ "funclet"(token %cp) \]' "$ir")" -eq 2
grep -q 'text.reg2mem' "$ir"
if grep -Eq 'c"(left|right)\\00"|%text = phi ' "$ir"; then
  echo "enstr did not lower/encrypt funclet PHI" >&2; exit 1
fi
echo "PASS enstr funclet PHI and operand bundles"

for fixture in callbr funclet; do
  target=aarch64-linux-android23
  if [[ $fixture == funclet ]]; then target=aarch64-pc-windows-msvc; fi
  for mode in enstr fla vmfla; do
    sed "s/attributes #0 = { noinline }/attributes #0 = { noinline \"vllvm.$mode\" }/" \
      "$ROOT/test/phi/test_phi_$fixture.ll" > "$OUT_DIR/$fixture-$mode.input.ll"
    ir="$OUT_DIR/$fixture-$mode.ll"
    "$CLANG" --target="$target" -Wno-override-module -Xclang -llvm-verify-each \
      -O0 -S -emit-llvm "$OUT_DIR/$fixture-$mode.input.ll" -o "$ir" \
      2> "$OUT_DIR/$fixture-$mode.log"
    "$LLVM_AS" "$ir" -o /dev/null
    grep -q 'PHI lowering skipped' "$OUT_DIR/$fixture-$mode.log"
    grep -q ' = phi ' "$ir"
    grep -q 'c"fallback\\00"' "$ir"
    if grep -Eq 'vllvm\.(enstr\.cache|fla\.const|vmfla\.const)|_decrypto' "$ir"; then
      echo "$fixture/$mode fallback partially transformed the function/string" >&2
      exit 1
    fi
    echo "PASS $fixture $mode fallback"
  done
done
