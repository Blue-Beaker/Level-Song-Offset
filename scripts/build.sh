#!/usr/bin/env zsh
#
# Build script for Level Song Offset (Geode Mod)
# Cross-compiles for Windows (Win64) using Clang + MSVC SDK on Linux
#

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
BUILD_DIR="$PROJECT_DIR/build"

# ---- Configuration (edit these paths if your setup differs) ----
export GEODE_SDK="$HOME/Develop/geode/Geode"
export MSVC_SDK_DIR="$HOME/Develop/geode/msvc-sdk"
export SPLAT_DIR="$HOME/.local/share/Geode/cross-tools/splat"
export CLANG_VER="19"
export LLVM_VER="19"
export LLVM_PATH="/usr/lib/llvm-19/bin"
export HOST_ARCH="x86_64"

# MSVC SDK subdirectories
export WINSDK_INCLUDE="${MSVC_SDK_DIR}/sdk/include"
export WINSDK_LIB="${MSVC_SDK_DIR}/sdk/lib"
export MSVC_INCLUDE="${MSVC_SDK_DIR}/crt/include"
export MSVC_LIB="${MSVC_SDK_DIR}/crt/lib"

# Toolchain file
TOOLCHAIN_FILE="$HOME/.local/share/Geode/cross-tools/clang-msvc-sdk/clang-msvc.cmake"

# ---- Validate dependencies ----
command -v cmake >/dev/null 2>&1 || { echo "❌ cmake not found. Install it first."; exit 1; }
command -v ninja >/dev/null 2>&1 || { echo "❌ ninja not found. Install it first."; exit 1; }

if [ ! -d "$GEODE_SDK" ]; then
    echo "❌ Geode SDK not found at: $GEODE_SDK"
    exit 1
fi

if [ ! -d "$MSVC_SDK_DIR" ]; then
    echo "❌ MSVC SDK not found at: $MSVC_SDK_DIR"
    echo "   Run: xwin --accept-license splat --output $MSVC_SDK_DIR"
    exit 1
fi

if [ ! -f "$TOOLCHAIN_FILE" ]; then
    echo "❌ Toolchain file not found at: $TOOLCHAIN_FILE"
    exit 1
fi

# ---- Clean build dir (optional: comment out to keep cache) ----
rm -rf "$BUILD_DIR"

# ---- CMake Configuration ----
echo "🔧 Configuring CMake..."
cmake -B "$BUILD_DIR" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_TOOLCHAIN_FILE="$TOOLCHAIN_FILE" \
    -DGEODE_TARGET_PLATFORM=Win64 \
    -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
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
