#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
SRC="$ROOT_DIR/test/complex/test_passes_complex.c"
OUT_DIR="${OUT_DIR:-$ROOT_DIR/test/complex/out}"

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

NO_DEBUG_ARGS=(-g0)

strip_binary() {
  local bin=$1
  if command -v strip >/dev/null 2>&1; then
    strip "$bin" >/dev/null 2>&1 || strip -x "$bin" >/dev/null 2>&1 || true
  fi
}

mkdir -p "$OUT_DIR"

"$VLLVM_CLANG" "${EXTRA_ARGS[@]}" "${NO_DEBUG_ARGS[@]}" -O0 "$SRC" \
  -o "$OUT_DIR/base"
strip_binary "$OUT_DIR/base"
set +e
"$OUT_DIR/base"
BASE_STATUS=$?
set -e

run_case() {
  local name=$1
  local ll="$OUT_DIR/$name.ll"
  local exe="$OUT_DIR/$name"
  local upper_name
  upper_name=$(printf "%s" "$name" | tr "[:lower:]" "[:upper:]")
  local mode_define="-DVLLVM_TEST_${upper_name}=1"
  # icall 的 crypt 断言属于 level 2 行为（1=纯间接调用）；vmfla 的 lvars
  # 断言属于 level 2 行为（1=仅平坦化，2=+局部变量结构化）。
  local config_args=()
  case "$name" in
  icall) config_args=(-mllvm -vllvm-config=icall=2) ;;
  vmfla) config_args=(-mllvm -vllvm-config=vmfla=2) ;;
  esac

  # icall/vmfla/bcf 用例需要保留值名字断言位置（icall 的 crypt 链、
  # vmfla 的表访问、bcf 的 seed/虚假块标签）；其余用例的断言依赖匿名编号。
  if [ "$name" = icall ] || [ "$name" = vmfla ] || [ "$name" = bcf ]; then
    "$VLLVM_CLANG" "${EXTRA_ARGS[@]}" "${NO_DEBUG_ARGS[@]}" -O0 -S \
      -fno-discard-value-names -emit-llvm "$mode_define" ${config_args[@]+"${config_args[@]}"} \
      "$SRC" -o "$ll"
  else
    "$VLLVM_CLANG" "${EXTRA_ARGS[@]}" "${NO_DEBUG_ARGS[@]}" -O0 -S \
      -emit-llvm "$mode_define" ${config_args[@]+"${config_args[@]}"} "$SRC" -o "$ll"
  fi

  "$VLLVM_CLANG" "${EXTRA_ARGS[@]}" "${NO_DEBUG_ARGS[@]}" -O0 \
    "$mode_define" ${config_args[@]+"${config_args[@]}"} "$SRC" -o "$exe"
  strip_binary "$exe"

  set +e
  "$exe"
  local status=$?
  set -e

  if [ "$status" -ne "$BASE_STATUS" ]; then
    echo "$name exit status mismatch: base=$BASE_STATUS pass=$status" >&2
    exit 1
  fi

  case "$name" in
  enstr)
    grep -q "_decrypto" "$ll"
    if grep -q "alpha:local-string-pass" "$ll"; then
      echo "enstr left plaintext alpha string in IR" >&2
      exit 1
    fi
    ;;
  icall)
    grep -q "register_func" "$ll"
    grep -Eq "i32 nest [0-9]+" "$ll"
    grep -q "ret x17" "$ll"
    if grep -q "vllvm.icall.index_array" "$ll"; then
      echo "icall packed indexes must be call-site constants" >&2
      exit 1
    fi
    # 被调方语义：icall 目标不允许被直接调用；调用点普通直调各自专属
    # 跳板（__vllvm_icall.*.call.<n>.<fn>），加密运算只在跳板内。
    STRIPPED_ICALL=$(awk 'BEGIN{keep=1}
      /^define/{keep = ($0 !~ /@__vllvm_icall\./)}
      keep{print}' "$ll")
    if printf '%s\n' "$STRIPPED_ICALL" | grep -Eq \
      "call [^@]*@(op_add|op_sub|op_mix|score_string|recursive_flow)\("; then
      echo "direct calls to icall targets must be rewritten" >&2
      exit 1
    fi
    if printf '%s\n' "$STRIPPED_ICALL" | grep -q "vllvm.icall.arg.crypt"; then
      echo "call sites must forward plaintext args" >&2
      exit 1
    fi
    # 地址被取用的 op_* 不加密，纯直调目标（如 recursive_flow）加密。
    grep -q "vllvm.icall.crypt" "$ll"
    grep -Eq "define internal .*@__vllvm_icall\.[0-9A-F]+\.call\.[0-9]+\.recursive_flow\(" "$ll"
    ;;
  ibr)
    grep -q "indirectbr" "$ll"
    ;;
  vmfla)
    grep -q "vllvm.vmfla.const.table.* global " "$ll"
    grep -q "define private .*vllvm.impl.*ptr %" "$ll"
    grep -q "call .*vllvm.impl.*ptr @vllvm.vmfla.const.table" "$ll"
    grep -q "getelementptr i32, ptr %" "$ll"
    # 保留值名字编译，操作数可能带名（%loadSwitchIndex...）或匿名编号。
    grep -Eq "icmp (ult|uge) i32 %[A-Za-z0-9._-]+, [0-9]+" "$ll"
    grep -q "store volatile i32 .*ptr %.*" "$ll"
    grep -Eq "xor i32 %[A-Za-z0-9._-]+, %[A-Za-z0-9._-]+" "$ll"
    # level 2：局部变量结构化结构体必须出现。
    grep -q "vllvm.localvars." "$ll"
    if grep -Eq "load volatile i32, ptr getelementptr \\(i32, ptr @vllvm\\.vmfla\\.const\\.table" "$ll"; then
      echo "vmfla table loads must use the impl table parameter" >&2
      exit 1
    fi
    if grep -Eq "vllvm\\.(localvars\\.table|fla\\.const\\.table|combined\\.const\\.table)|func_index_table" "$ll"; then
      echo "vmfla must use one vmfla integer constant table" >&2
      exit 1
    fi
    # vmfla 不再内嵌间接调用：不允许出现 func_table 调用表。
    if grep -q "func_table" "$ll"; then
      echo "vmfla must not emit a func_table" >&2
      exit 1
    fi
    ;;
  bcf)
    # 独立 BCF：模块级共享导出 seed export_write（函数内不初始化）、
    # 随机指令虚假块（锚定在永不可达分支后继上）、对随机常量比较的
    # 恒定谓词（(seed op K) == 恒假 / != 恒真随机）；不得触发 vmfla
    # 链路产物。
    grep -q "@export_write" "$ll"
    grep -Eq "^vllvm\.bcf\.fake\." "$ll"
    grep -q "label %vllvm.bcf.fake" "$ll"
    grep -q "vllvm.bcf.cmp" "$ll"
    grep -Eq "icmp (eq|ne)" "$ll"
    if grep -q "vllvm.vmfla.const.table" "$ll"; then
      echo "bcf must stay independent of the vmfla chain" >&2
      exit 1
    fi
    ;;
  esac
}

# run_case enstr
# run_case ibr
run_case bcf
run_case icall
run_case vmfla
