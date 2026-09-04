#!/usr/bin/env bash
set -euo pipefail
ROOT=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
OUT_DIR="${OUT_DIR:-$ROOT/test/android/out}"
SDK="${ANDROID_SDK_ROOT:-/Users/work/Library/Android/sdk}"
NDK="${ANDROID_NDK_ROOT:-$SDK/ndk/27.0.12077973}"
NDK_BIN="$NDK/toolchains/llvm/prebuilt/darwin-x86_64/bin"
mkdir -p "$OUT_DIR/core"
failed=0
for mode in baseline ibr enstr fla icall lvars bcf vmfla vmp combined; do
  objects="$OUT_DIR/$mode/build/CMakeFiles/game.dir"
  if ! "$NDK_BIN/aarch64-linux-android23-clang++" -O2 -std=c++17 \
    -I"$OUT_DIR/source" -I"$NDK/sources/android/native_app_glue" \
    "$ROOT/test/android/endless_tunnel_core.cpp" \
    "$objects/util.cpp.o" "$objects/obstacle.cpp.o" "$objects/obstacle_generator.cpp.o" \
    -static-libstdc++ -Wl,--gc-sections -lm -llog -landroid -lEGL -lGLESv2 -lOpenSLES \
    -o "$OUT_DIR/core/$mode" >"$OUT_DIR/core/$mode.build.log" 2>&1; then
    echo "FAIL core link: $mode"
    failed=1
  else
    echo "PASS core link: $mode"
  fi
done
exit "$failed"
