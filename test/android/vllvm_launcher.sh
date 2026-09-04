#!/usr/bin/env bash
set -euo pipefail

# Keep NDK headers/linker and use the normal VLLVM Clang compilation pipeline.
shift
args=("$@")
output=""
for ((i=0; i<${#args[@]}; ++i)); do
  if [[ ${args[i]} == -o ]]; then output=${args[i+1]}; fi
done
[[ -n "$output" ]] || { echo "missing compiler output" >&2; exit 1; }
ir="${output%.o}.ll"
extra=(-resource-dir "$VLLVM_NDK_RESOURCE" -Wno-error -Wno-pragma-clang-attribute -Wno-unused-command-line-argument)
# Verify each pass for all game units; GLM is unmodified third-party code.
case "$output" in
  glm/*) ;;
  *) extra+=(-Xclang -llvm-verify-each) ;;
esac
if [[ -n ${VLLVM_TEST_ANNOTATION:-} ]]; then
  extra+=("-DVLLVM_TEST_ANNOTATION=\"vllvm:${VLLVM_TEST_ANNOTATION}\"")
fi
# Compile the original source separately; do not rerun obfuscation on transformed IR.
"$VLLVM_TEST_CLANG" "${args[@]}" "${extra[@]}" -Rpass-missed=vmp
"$VLLVM_TEST_CLANG" "${args[@]}" "${extra[@]}" -Rpass-missed=vmp -S -emit-llvm -o "$ir"
