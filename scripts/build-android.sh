#!/usr/bin/env zsh
#
# Build script for Level Song Offset (Geode Mod) - Android
# Cross-compiles for Android64 (arm64-v8a) using Android NDK
#

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
BUILD_DIR="$PROJECT_DIR/build-android"

# ---- Configuration ----
export GEODE_SDK="$HOME/Develop/geode/Geode"
export ANDROID_HOME="$HOME/Android/AndroidSDK"
export ANDROID_NDK_HOME="$ANDROID_HOME/ndk/29.0.14206865"
export JAVA_HOME="/usr/lib/jvm/temurin-21-jdk-amd64"

# Android target config
ANDROID_API_LEVEL="23"
ANDROID_TARGET="Android64"   # or Android32 for armeabi-v7a
ANDROID_ABI="arm64-v8a"      # or armeabi-v7a for 32-bit
ANDROID_PLATFORM="android-${ANDROID_API_LEVEL}"

# NDK toolchain file
TOOLCHAIN_FILE="${ANDROID_NDK_HOME}/build/cmake/android.toolchain.cmake"

# ---- Validate dependencies ----
command -v cmake >/dev/null 2>&1 || { echo "❌ cmake not found"; exit 1; }
command -v ninja >/dev/null 2>&1 || { echo "❌ ninja not found"; exit 1; }

if [ ! -d "$GEODE_SDK" ]; then
    echo "❌ Geode SDK not found at: $GEODE_SDK"
    exit 1
fi

if [ ! -f "$TOOLCHAIN_FILE" ]; then
    echo "❌ Android NDK toolchain not found at: $TOOLCHAIN_FILE"
    exit 1
fi

if [ ! -d "$ANDROID_HOME/platforms/$ANDROID_PLATFORM" ]; then
    echo "❌ Android platform $ANDROID_PLATFORM not found"
    exit 1
fi

# ---- Optional clean ----
if [[ "$*" == *"clean"* ]] || [ -n "$CLEAN" ]; then
    echo "🧹 Cleaning build directory..."
    rm -rf "$BUILD_DIR"
fi

# ---- CMake Configuration ----
echo "🔧 Configuring CMake for Android64 (arm64-v8a)..."
cmake -B "$BUILD_DIR" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_TOOLCHAIN_FILE="$TOOLCHAIN_FILE" \
    -DANDROID_ABI="$ANDROID_ABI" \
    -DANDROID_PLATFORM="$ANDROID_PLATFORM" \
    -DANDROID_STL=c++_shared \
    -DGEODE_TARGET_PLATFORM="$ANDROID_TARGET" \
    -DGEODE_BINDINGS_REPO_PATH="$PROJECT_DIR/build/_deps/bindings-src" \
    -DCPM_SOURCE_CACHE="$HOME/.cache/CPM_SOURCE_CACHE" \
    -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
    -DCMAKE_CXX_COMPILER_LAUNCHER=ccache \
    2>&1

# ---- Build ----
echo ""
echo "🔨 Building..."
cmake --build "$BUILD_DIR" 2>&1

echo ""
GEODE_PKG=$(find "$BUILD_DIR" -name '*.geode' -type f 2>/dev/null | head -1)
if [ -n "$GEODE_PKG" ]; then
    echo "✅ Build complete!"
    echo "   Output: $GEODE_PKG"
else
    echo "⚠️  Build finished but no .geode package found."
fi
