#!/usr/bin/env bash
set -euo pipefail
ROOT=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
TEST_DIR="$ROOT/test/android"
SAMPLE_ROOT="$ROOT/test/app/ndk-samples"
OUT_DIR="${OUT_DIR:-$TEST_DIR/out}"
SDK="${ANDROID_SDK_ROOT:-/Users/work/Library/Android/sdk}"
NDK="${ANDROID_NDK_ROOT:-$SDK/ndk/27.0.12077973}"
BUILD_TOOLS="${ANDROID_BUILD_TOOLS:-$SDK/build-tools/34.0.0}"
export JAVA_HOME="${JAVA_HOME:-/Users/work/Library/Java/JavaVirtualMachines/jbr-21.0.11/Contents/Home}"
export PATH="/opt/homebrew/bin:$JAVA_HOME/bin:$PATH"
export VLLVM_TEST_CLANG="${CLANG:-$ROOT/build/llvm-macos/bin/clang++}"

# NDK 预构建目录按宿主平台变化；windows 下 apksigner 是 .bat。
case "$(uname -s)" in
  Darwin) HOST_TAG=darwin-x86_64; EXE="" ;;
  Linux) HOST_TAG=linux-x86_64; EXE="" ;;
  *) HOST_TAG=windows-x86_64; EXE=".bat" ;;
esac
NDK_PREBUILT="$NDK/toolchains/llvm/prebuilt/$HOST_TAG"
CLANG_RESOURCE_DIR=$(ls -d "$NDK_PREBUILT"/lib/clang/* | tail -1)
export VLLVM_NDK_RESOURCE="$CLANG_RESOURCE_DIR"
# 平台 jar 取 SDK 内最新版本（默认路径仍指向脚本原 macOS 环境的 android-34）。
PLATFORM_JAR="${ANDROID_PLATFORM_JAR:-$SDK/platforms/android-34/android.jar}"
[[ -f "$PLATFORM_JAR" ]] || PLATFORM_JAR=$(ls -d "$SDK"/platforms/android-*/android.jar | sort -V | tail -1)
# Git Bash 无 zip；用 python 重打包（读出 aapt2 产物再追加 so）。
add_so_to_apk() {
  local apk=$1 so=$2 entry=$3
  python3 - "$apk" "$so" "$entry" <<'PYEOF'
import sys, zipfile
apk, so, entry = sys.argv[1:4]
with zipfile.ZipFile(apk) as zin:
    items = [(i, zin.read(i.filename)) for i in zin.infolist()]
with open(so, "rb") as f:
    data = f.read()
with zipfile.ZipFile(apk, "w") as zout:
    for info, blob in items:
        # Android R+ 要求 resources.arsc 无压缩存储；其余条目保持原压缩方式。
        method = (zipfile.ZIP_STORED if info.filename == "resources.arsc"
                  else info.compress_type)
        zout.writestr(info.filename, blob, compress_type=method)
    zout.writestr(entry, data)
PYEOF
}

# Git Bash 下 cmake 的分号列表参数不做 MSYS 路径转换，元素会以 /d/...
# 形式传给原生 cmake 导致读不到；转成 D:/ 形式。
native_path() {
  case "$(uname -s)" in
    MINGW*|MSYS*|CYGWIN*) cygpath -m "$1" ;;
    *) printf '%s' "$1" ;;
  esac
}

[[ -d "$SAMPLE_ROOT/.git" ]] || { echo "clone android/ndk-samples into test/app/ndk-samples first" >&2; exit 1; }
mkdir -p "$OUT_DIR/source" "$OUT_DIR/apk" "$OUT_DIR/cmake"
# Import only the Android helper; mixing entire CMake module versions breaks probes.
cp "$SDK/cmake/3.22.1/share/cmake-3.22/Modules/AndroidNdkModules.cmake" "$OUT_DIR/cmake/"
cp -R "$SAMPLE_ROOT/endless-tunnel/app/src/main/cpp/." "$OUT_DIR/source/"
perl "$TEST_DIR/annotate_endless_tunnel.pl" "$OUT_DIR/source"
# NDK r27's legacy toolchain lacks CMake's newer WHOLE_ARCHIVE feature mapping.
perl -i -pe 's/\$<LINK_LIBRARY:WHOLE_ARCHIVE,native_app_glue>/"-Wl,--whole-archive" native_app_glue "-Wl,--no-whole-archive"/g' "$OUT_DIR/source/CMakeLists.txt"

# This is an isolated local test key, never a production signing key.
if [[ ! -f "$OUT_DIR/test.keystore" ]]; then
  keytool -genkeypair -keystore "$OUT_DIR/test.keystore" -alias vllvm-test \
    -storepass android -keypass android -keyalg RSA -keysize 2048 -validity 3650 \
    -dname 'CN=VLLVM Local Test' >/dev/null 2>&1
fi

modes=("$@")
if [[ ${#modes[@]} == 0 ]]; then
  modes=(baseline ibr enstr fla icall lvars bcf vmfla vmp combined)
fi
failed=0
for mode in "${modes[@]}"; do
  case "$mode" in
    baseline) export VLLVM_TEST_ANNOTATION="" ;;
    combined) export VLLVM_TEST_ANNOTATION="enstr,bcf,lvars,fla,icall,ibr" ;;
    ibr|enstr|fla|icall|lvars|bcf|vmfla|vmp|bb2func|merge) export VLLVM_TEST_ANNOTATION="$mode" ;;
    *) echo "unknown mode: $mode" >&2; exit 1 ;;
  esac
  # launcher 前段必须是原生 bash.exe 全路径：裸 "bash" 在 Windows 上会
  # 命中 WSL 的 bash；后段脚本路径由该 bash 自己解析，保持 POSIX 形式。
  mode_dir="$OUT_DIR/$mode"
  mkdir -p "$mode_dir/package/lib/arm64-v8a"
  echo "Building $mode (${VLLVM_TEST_ANNOTATION:-no passes})"
  if ! (
    cmake -S "$OUT_DIR/source" -B "$mode_dir/build" -G Ninja \
      -DCMAKE_TOOLCHAIN_FILE="$NDK/build/cmake/android.toolchain.cmake" \
      -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-23 \
      -DANDROID_STL=c++_static -DANDROID_SUPPORT_FLEXIBLE_PAGE_SIZES=ON \
      -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_FLAGS_RELEASE='-O2 -DNDEBUG' \
      "-DCMAKE_MODULE_PATH=$(native_path "$SAMPLE_ROOT/cmake");$(native_path "$OUT_DIR/cmake")" \
      -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
      "-DCMAKE_CXX_COMPILER_LAUNCHER=$(native_path "$(command -v bash)");$TEST_DIR/vllvm_launcher.sh"
    cmake --build "$mode_dir/build" --parallel "${JOBS:-4}" -- -k 0
  ) >"$mode_dir/build.log" 2>&1; then
    echo "FAIL $mode: $mode_dir/build.log"
    failed=1
    continue
  fi
  cp "$mode_dir/build/libgame.so" "$mode_dir/package/lib/arm64-v8a/"
  "$BUILD_TOOLS/aapt2" link -o "$mode_dir/resources.apk" \
    --manifest "$TEST_DIR/AndroidManifest.xml" -I "$PLATFORM_JAR" \
    --rename-manifest-package "com.vllvm.test.endlesstunnel.$mode"
  cp "$mode_dir/resources.apk" "$mode_dir/unsigned.apk"
  add_so_to_apk "$mode_dir/unsigned.apk" \
    "$mode_dir/package/lib/arm64-v8a/libgame.so" "lib/arm64-v8a/libgame.so"
  "$BUILD_TOOLS/zipalign" -f -p 4 "$mode_dir/unsigned.apk" "$mode_dir/aligned.apk"
  "$BUILD_TOOLS/apksigner$EXE" sign --ks "$OUT_DIR/test.keystore" --ks-key-alias vllvm-test \
    --ks-pass pass:android --key-pass pass:android \
    --out "$OUT_DIR/apk/endless-tunnel-$mode.apk" "$mode_dir/aligned.apk"
  "$BUILD_TOOLS/apksigner$EXE" verify "$OUT_DIR/apk/endless-tunnel-$mode.apk"
  echo "PASS $mode: $OUT_DIR/apk/endless-tunnel-$mode.apk"
done
exit "$failed"
