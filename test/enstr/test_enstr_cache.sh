#!/usr/bin/env bash
set -euo pipefail
ROOT=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
if [[ -z ${CLANG:-} ]]; then
  CLANG=$ROOT/build/llvm-macos/bin/clang
  [[ -x $CLANG ]] || CLANG=$ROOT/build/llvm-linux/bin/clang
fi
LLVM_AS=${LLVM_AS:-$(dirname "$CLANG")/llvm-as}
OUT_DIR=${OUT_DIR:-$ROOT/test/enstr/out/cache}
mkdir -p "$OUT_DIR"
args=(-Wno-override-module -Xclang -llvm-verify-each)
if [[ $(uname -s) == Darwin ]]; then args+=(-isysroot "$(xcrun --show-sdk-path)"); fi

for opt in 0 2; do
  ir="$OUT_DIR/O$opt.ll"
  "$CLANG" "${args[@]}" -O"$opt" -S -emit-llvm \
    "$ROOT/test/enstr/test_enstr_cache.ll" -o "$ir"
  "$LLVM_AS" "$ir" -o /dev/null
  # 整个模块只有一个匿名池：一个 base、一次申请、一次解密发布。
  # 池逻辑来自链入的 C 运行时；描述表/flags 是 internal 常量，O2 会把
  # 它们折叠进指令，相关断言只在 O0 做。
  [[ $(grep -Ec '^@vllvm_enstr_pool_base = internal global ptr null' "$ir") == 1 ]]
  if [[ $opt == 0 ]]; then
    [[ $(grep -Ec '^@__vllvm_enstr_table = internal .* constant ptr @' "$ir") == 1 ]]
    grep -q '@__vllvm_enstr_mmap_flags = internal .* constant i32 34' "$ir"
    [[ $(grep -Ec 'define .*@__vllvm_enstr_get\(' "$ir") == 1 ]]
  fi
  grep -Eq 'load atomic (ptr|i64).*acquire' "$ir"
  grep -q 'cmpxchg ptr.*acq_rel acquire' "$ir"
  grep -Eq 'store atomic (ptr|i64).*release' "$ir"
  grep -q 'call void @llvm.trap' "$ir"
  # 下标访问：直接引用与指针别名都走 get(i64 下标)。O2 内联后看不到调用形态。
  if [[ $opt == 0 ]]; then
    [[ $(grep -Ec 'call ptr @__vllvm_enstr_get\(i64 [0-9]+\)' "$ir") -ge 3 ]]
  fi
  if grep -Eq 'cache-alpha|cache-beta' "$ir"; then
    echo 'plaintext string survived encryption' >&2; exit 1
  fi
  if grep -Eq '@(free|munmap)\(' "$ir"; then
    echo 'pool must retain its process lifetime' >&2; exit 1
  fi

  # 统计匿名内存申请次数并拦截 OOM trap。按宿主平台改写对应入口。
  sed -e 's/@mmap(/@enstr_test_mmap(/g' \
      -e 's/@VirtualAlloc(/@enstr_test_virtual_alloc(/g' \
      -e 's/@llvm.trap(/@enstr_test_trap(/g' \
      -e 's/^declare .*@enstr_test_mmap(.*/declare ptr @enstr_test_mmap(ptr, i64, i32, i32, i32, i64)/' \
      -e 's/^declare .*@enstr_test_virtual_alloc(.*/declare ptr @enstr_test_virtual_alloc(ptr, i64, i32, i32)/' \
      -e 's/^declare void @enstr_test_trap().*/declare void @enstr_test_trap()/' \
      "$ir" > "$OUT_DIR/counted_O$opt.ll"
  "$LLVM_AS" "$OUT_DIR/counted_O$opt.ll" -o /dev/null
  "$CLANG" "${args[@]}" -O"$opt" -Xclang -disable-llvm-passes -c \
    "$OUT_DIR/counted_O$opt.ll" -o "$OUT_DIR/O$opt.o"
  "$CLANG" "${args[@]}" -std=c11 -O2 -pthread \
    "$ROOT/test/enstr/test_enstr_cache_driver.c" "$OUT_DIR/O$opt.o" \
    -o "$OUT_DIR/O$opt"
  "$OUT_DIR/O$opt" single
  # Separate processes ensure every concurrent run starts with an empty pool.
  for run in 1 2 3; do "$OUT_DIR/O$opt" threads; done
  "$OUT_DIR/O$opt" oom
done

# IR inputs need an explicit layout when the local LLVM lacks the X86 backend.
sed '1s/^/target datalayout = "e-p:32:32"\n/' \
  "$ROOT/test/enstr/test_enstr_cache.ll" > "$OUT_DIR/i386-input.ll"
# Cross-target verifier coverage also checks the target-sized allocation size.
for target in aarch64-linux-android23 i386-unknown-linux-gnu; do
  input="$ROOT/test/enstr/test_enstr_cache.ll"
  if [[ $target == i386-* ]]; then input="$OUT_DIR/i386-input.ll"; fi
  "$CLANG" --target="$target" -Wno-override-module -Xclang -llvm-verify-each \
    -O0 -S -emit-llvm "$input" \
    -o "$OUT_DIR/$target.ll"
  "$LLVM_AS" "$OUT_DIR/$target.ll" -o /dev/null
done
# 12 + 11 字节按 16 对齐各占一槽：池大小 32；mmap 调用来自链入的 C
# 运行时，参数宽度固定为 i64（LP64 位码），-O0 下常量经全局加载。
grep -Eq 'call ptr @mmap\(ptr( noundef)? null, i64' "$OUT_DIR/i386-unknown-linux-gnu.ll"
# Android 目标确认 ELF 常量：MAP_PRIVATE|MAP_ANONYMOUS=0x22。
grep -q '@__vllvm_enstr_mmap_flags = internal .* constant i32 34' "$OUT_DIR/aarch64-linux-android23.ll"
grep -Eq 'call ptr @mmap\(ptr( noundef)? null, i64' "$OUT_DIR/aarch64-linux-android23.ll"
echo 'PASS enstr pool: linked C runtime; one anonymous region; index access; races'
