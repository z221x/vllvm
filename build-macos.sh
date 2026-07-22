#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)

LLVM_VERSION=${LLVM_VERSION:-21.1.0}
LLVM_SRC=${LLVM_SRC:-"$ROOT_DIR/llvm-project-$LLVM_VERSION"}
LLVM_BUILD=${LLVM_BUILD:-"$ROOT_DIR/build/llvm-macos"}
LLVM_INSTALL_PREFIX=${LLVM_INSTALL_PREFIX:-"$ROOT_DIR/out/llvm-macos"}
CC=${CC:-clang}
CXX=${CXX:-clang++}
JOBS=${JOBS:-$(sysctl -n hw.ncpu 2>/dev/null || echo 8)}
NO_CLONE=${NO_CLONE:-0}
PATCH_FILE="$ROOT_DIR/patches/llvm-21.1-vllvm.patch"
VMP_PATCH_FILE="$ROOT_DIR/patches/llvm-21.1-vmp.patch"
VMP_CALL_PATCH_FILE="$ROOT_DIR/patches/llvm-21.1-vmp-call.patch"

require_command() {
  if ! command -v "$1" >/dev/null 2>&1; then
    echo "missing command: $1" >&2
    exit 1
  fi
}

clone_llvm_if_needed() {
  if [ -d "$LLVM_SRC/llvm" ]; then
    return
  fi
  if [ "$NO_CLONE" = "1" ]; then
    echo "LLVM source not found: $LLVM_SRC" >&2
    exit 1
  fi

  require_command git
  git -c advice.detachedHead=false clone --depth 1 --branch "llvmorg-$LLVM_VERSION" \
    https://github.com/llvm/llvm-project.git "$LLVM_SRC"
  git -C "$LLVM_SRC" switch -c "vllvm-llvmorg-$LLVM_VERSION" >/dev/null
}

copy_vllvm_sources() {
  local dst="$LLVM_SRC/llvm/lib/Transforms/VLLVM"
  local public_include="$LLVM_SRC/llvm/include/llvm/Transforms/VLLVM"
  local vmp_target="$LLVM_SRC/llvm/lib/Target/VMP"

  cmake -E make_directory "$dst/include"
  cmake -E make_directory "$public_include"
  cmake -E copy_directory "$ROOT_DIR/src/vmtarget/VMP" "$vmp_target"

  cmake -E copy_if_different "$ROOT_DIR/src/CMakeLists.txt" "$dst/CMakeLists.txt"
  for file in "$ROOT_DIR"/src/*.cpp; do
    cmake -E copy_if_different "$file" "$dst/$(basename "$file")"
  done
  for file in "$ROOT_DIR"/src/include/*.h; do
    cmake -E copy_if_different "$file" "$dst/include/$(basename "$file")"
  done
  cmake -E copy_directory "$ROOT_DIR/src/vminterpreter" "$dst/vminterpreter"
  cmake -E copy_if_different "$ROOT_DIR/src/include/VLLVM.h" \
    "$public_include/VLLVM.h"
  cmake -E copy_if_different "$ROOT_DIR/src/include/VmpCommon.h" \
    "$public_include/VmpCommon.h"
}

apply_one_patch() {
  local patch_file=$1
  if git -C "$LLVM_SRC" apply --reverse --check --unidiff-zero \
      "$patch_file" >/dev/null 2>&1; then
    echo "$(basename "$patch_file") is already applied"
    return
  fi

  git -C "$LLVM_SRC" apply --check "$patch_file"
  git -C "$LLVM_SRC" apply "$patch_file"
}

configure_and_build() {
  local args=(
    -S "$LLVM_SRC/llvm"
    -B "$LLVM_BUILD"
    -G Ninja
    -DCMAKE_C_COMPILER="$CC"
    -DCMAKE_CXX_COMPILER="$CXX"
    -DCMAKE_BUILD_TYPE=Release
    -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
    -DCMAKE_INSTALL_PREFIX="$LLVM_INSTALL_PREFIX"
    "-DLLVM_ENABLE_PROJECTS=clang;clang-tools-extra;lld"
    -DCLANG_DEFAULT_LINKER=lld
    "-DLLVM_TARGETS_TO_BUILD=host;AArch64"
    -DLLVM_EXPERIMENTAL_TARGETS_TO_BUILD=VMP
    -DLLVM_INCLUDE_TESTS=OFF
    -DLLVM_INCLUDE_EXAMPLES=OFF
    -DLLVM_INCLUDE_BENCHMARKS=OFF
    -DLLVM_ENABLE_ZLIB=OFF
    -DLLVM_ENABLE_ZSTD=OFF
    -DLLVM_ENABLE_LIBXML2=OFF
    -DLLVM_ENABLE_TERMINFO=OFF
  )
  if [ -n "${MACOSX_DEPLOYMENT_TARGET:-}" ]; then
    args+=("-DCMAKE_OSX_DEPLOYMENT_TARGET=$MACOSX_DEPLOYMENT_TARGET")
  fi
  if [ -n "${CMAKE_OSX_ARCHITECTURES:-}" ]; then
    args+=("-DCMAKE_OSX_ARCHITECTURES=$CMAKE_OSX_ARCHITECTURES")
  fi

  cmake "${args[@]}"
  cmake --build "$LLVM_BUILD" --target clang clangd lld llc --parallel "$JOBS"
}

require_command cmake
require_command ninja
require_command "$CC"
require_command "$CXX"

clone_llvm_if_needed
copy_vllvm_sources
apply_one_patch "$PATCH_FILE"
apply_one_patch "$VMP_PATCH_FILE"
apply_one_patch "$VMP_CALL_PATCH_FILE"
configure_and_build

echo "vllvm clang build finished:"
echo "  $LLVM_BUILD/bin/clang"
echo "  $LLVM_BUILD/bin/clangd"
echo "  $LLVM_BUILD/bin/llc"
echo "example:"
echo "  $LLVM_BUILD/bin/clang input.c -o input"
