#!/bin/bash
# Cross-compile pulley-runner for Android x86 using CMake + NDK toolchain.
#
# Prerequisites:
#   1. Build the C API for i686-linux-android:
#      bash ci/wasmline/build-artifacts.sh pulley-min x86-android i686-linux-android capi
#
#   2. Set ANDROID_NDK_HOME (or pass as $1)
#
# Usage:
#   bash build-runner.sh [/path/to/android-ndk]
#
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
NDK="${1:-${ANDROID_NDK_HOME:-${ANDROID_NDK:-}}}"

if [[ -z "$NDK" ]]; then
  echo "ERROR: Android NDK path not set."
  echo "  Set ANDROID_NDK_HOME or pass as argument: bash build-runner.sh /path/to/ndk"
  exit 1
fi

# Paths
TOOLCHAIN_FILE="$NDK/build/cmake/android.toolchain.cmake"
WASMTIME_CAPI="$SCRIPT_DIR/wasmtime-dev-x86-android-c-api"
BUILD_DIR="$SCRIPT_DIR/build-runner"
OUTPUT="$SCRIPT_DIR/pulley-runner"

if [[ ! -f "$TOOLCHAIN_FILE" ]]; then
  echo "ERROR: NDK toolchain file not found at: $TOOLCHAIN_FILE"
  exit 1
fi

if [[ ! -d "$WASMTIME_CAPI/include" ]]; then
  echo "ERROR: C API headers not found at: $WASMTIME_CAPI/include"
  exit 1
fi

if [[ ! -f "$WASMTIME_CAPI/lib/libwasmtime.a" ]]; then
  echo "ERROR: libwasmtime.a not found at: $WASMTIME_CAPI/lib"
  exit 1
fi


echo "Cross-compiling pulley-runner for i686-linux-android ..."
echo "  NDK:            $NDK"
echo "  Wasmtime C API: $WASMTIME_CAPI"

# Clean and create build dir
rm -rf "$BUILD_DIR"
mkdir -p "$BUILD_DIR"

# Configure with CMake
cmake \
  -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE="$TOOLCHAIN_FILE" \
  -DANDROID_ABI=x86 \
  -DANDROID_PLATFORM=android-21 \
  -DWASMTIME_CAPI_DIR="$WASMTIME_CAPI" \
  -DCMAKE_BUILD_TYPE=Release \
  -S "$SCRIPT_DIR" \
  -B "$BUILD_DIR"

# Build
cmake --build "$BUILD_DIR"

# Copy output
cp "$BUILD_DIR/pulley-runner" "$OUTPUT"

echo ""
echo "Build successful: $OUTPUT"
echo ""
echo "=== Next steps ==="
echo "  adb push $OUTPUT       /data/local/tmp/pulley-runner"
echo "  adb push your.pwasm    /data/local/tmp/your.pwasm"
echo "  adb shell chmod +x /data/local/tmp/pulley-runner"
echo "  adb shell /data/local/tmp/pulley-runner /data/local/tmp/your.pwasm"
