#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
SRC="$ROOT_DIR/test/indirectbranch/test_indirectbr.c"
PHI_SRC="$ROOT_DIR/test/indirectbranch/test_indirectbr_phi.ll"
OUT_DIR="${OUT_DIR:-$ROOT_DIR/test/indirectbranch/out}"

if [ -n "${CLANG:-}" ]; then
  VLLVM_CLANG="$CLANG"
elif [ -x "$ROOT_DIR/build/llvm-macos/bin/clang" ]; then
  VLLVM_CLANG="$ROOT_DIR/build/llvm-macos/bin/clang"
elif [ -x "$ROOT_DIR/build/llvm-linux/bin/clang" ]; then
  VLLVM_CLANG="$ROOT_DIR/build/llvm-linux/bin/clang"
else
  VLLVM_CLANG="clang"
fi

EXTRA_ARGS=()
if [ "$(uname -s)" = "Darwin" ] && command -v xcrun >/dev/null 2>&1; then
  SDK_PATH=$(xcrun --show-sdk-path 2>/dev/null || true)
  if [ -n "$SDK_PATH" ]; then
    EXTRA_ARGS+=(-isysroot "$SDK_PATH")
  fi
fi

mkdir -p "$OUT_DIR"

check_indirect_destinations() {
  local ll=$1
  if ! awk '
    /^[[:space:]]*indirectbr ptr / {
      line = $0
      destinations = gsub(/label %/, "", line)
      if (destinations < 2 || destinations > 4)
        invalid = 1
      ++seen
    }
    END { exit seen == 0 || invalid }
  ' "$ll"; then
    echo "every indirectbr must have between two and four destinations" >&2
    exit 1
  fi
}

check_dynamic_index() {
  local ll=$1
  local function_name=$2
  if ! grep -Eq '^@vllvm\.ibr\.table = private (unnamed_addr )?constant \[[0-9]+ x ptr\]' "$ll"; then
    echo "block table must be a constant array of plain addresses" >&2
    exit 1
  fi

  if ! awk -v function_name="$function_name" '
    index($0, "@vllvm.ibr.table =") == 1 {
      table_line = $0
      table_blocks += gsub("blockaddress\\(@" function_name ",", "", table_line)
    }
    index($0, "define ") == 1 &&
        index($0, "@" function_name "(") != 0 {
      in_function = 1
      saw_first_block = 0
      next
    }
    in_function && /^}/ { in_function = 0 }
    in_function && /^[[:alnum:]$._-]+:/ {
      ++function_blocks
      saw_first_block = 1
      next
    }
    in_function && !saw_first_block && /^[[:space:]]+[^;[:space:]}]/ {
      ++function_blocks
      saw_first_block = 1
    }
    END { exit table_blocks == 0 || table_blocks != function_blocks }
  ' "$ll"; then
    echo "block table must contain every block in the protected function" >&2
    exit 1
  fi

  if ! awk '
    $3 == "alloca" && $4 ~ /^i32/ { allocas[$1] = 1 }
    $3 == "load" && $4 == "volatile" && $5 ~ /^i32,/ {
      ptr = $7
      sub(/,$/, "", ptr)
      if (allocas[ptr])
        saw_load = 1
    }
    $1 == "store" && $2 == "volatile" && $3 == "i32" {
      ptr = $6
      sub(/,$/, "", ptr)
      if (allocas[ptr])
        saw_store = 1
    }
    END { exit !(saw_load && saw_store) }
  ' "$ll"; then
    echo "IndirectBranchPass must use a writable volatile index state" >&2
    exit 1
  fi

  if ! awk '
    /getelementptr inbounds .*@vllvm\.ibr\.table/ {
      ++seen
      if ($0 !~ /, i32 %[[:alnum:]_.-]+$/)
        invalid = 1
    }
    END { exit seen == 0 || invalid }
  ' "$ll"; then
    echo "block table accesses must use the decoded runtime index" >&2
    exit 1
  fi

  if ! awk '
    /^[[:space:]]*indirectbr ptr / { ++branches }
    /^[[:space:]]*%.* = select i1 / { ++selections }
    END { exit branches == 0 || selections < branches }
  ' "$ll"; then
    echo "every indirect branch must include a fake index candidate" >&2
    exit 1
  fi
}

for opt in 0 2; do
  base="$OUT_DIR/test_indirectbr_base_O$opt"
  transformed="$OUT_DIR/test_indirectbr_O$opt"
  ll="$OUT_DIR/test_indirectbr_O$opt.ll"

  "$VLLVM_CLANG" "${EXTRA_ARGS[@]}" -g0 -O"$opt" "$SRC" -o "$base"
  "$VLLVM_CLANG" "${EXTRA_ARGS[@]}" -g0 -O"$opt" -DVLLVM_TEST_IBR=1 \
    "$SRC" -o "$transformed"
  "$VLLVM_CLANG" "${EXTRA_ARGS[@]}" -g0 -O"$opt" -DVLLVM_TEST_IBR=1 \
    -S -emit-llvm "$SRC" -o "$ll"

  base_output=$("$base")
  ibr_output=$("$transformed")
  if [ "$base_output" != "$ibr_output" ]; then
    echo "O$opt indirect branch output mismatch" >&2
    echo "base: $base_output" >&2
    echo "ibr:  $ibr_output" >&2
    exit 1
  fi

  check_indirect_destinations "$ll"
  check_dynamic_index "$ll" main
  if grep -q "vllvm.ibr.fake" "$ll"; then
    echo "IndirectBranchPass must reuse existing blocks as fake targets" >&2
    exit 1
  fi
  if [ "$opt" -eq 0 ]; then
    # O2 may fold the deliberately untouched switch into a new direct branch
    # after this pass. O0 preserves the input CFG and checks every original br.
    if awk '
      /^define .*@main\(/ { in_main = 1 }
      in_main && /^}/ { in_main = 0 }
      in_main && /^[[:space:]]*br / { found = 1 }
      END { exit !found }
    ' "$ll"; then
      echo "all original br terminators must be indirect" >&2
      exit 1
    fi

    if ! grep -q "^  switch " "$ll"; then
      echo "IndirectBranchPass must not rewrite switch terminators" >&2
      exit 1
    fi
  fi
done

PHI_LL="$OUT_DIR/test_indirectbr_phi.ll"
"$VLLVM_CLANG" -Wno-override-module -O0 -x ir -S -emit-llvm "$PHI_SRC" \
  -o "$PHI_LL"
"$VLLVM_CLANG" -Wno-override-module -O0 -x ir -c "$PHI_SRC" \
  -o "$OUT_DIR/test_indirectbr_phi.o"
check_indirect_destinations "$PHI_LL"
check_dynamic_index "$PHI_LL" ibr_phi
if grep -q " phi " "$PHI_LL"; then
  echo "IndirectBranchPass must remove PHI nodes before adding fake edges" >&2
  exit 1
fi
