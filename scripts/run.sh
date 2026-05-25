#!/usr/bin/env zsh
#
# Run script for Level Song Offset (Geode Mod)
# Launches Geometry Dash via Steam to test the mod
#

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
BUILD_DIR="$PROJECT_DIR/build"

GEODE_PACKAGE=$(find "$BUILD_DIR" -name '*.geode' -type f 2>/dev/null | head -1)

if [ -z "$GEODE_PACKAGE" ]; then
    echo "⚠️  No .geode package found in build directory."
    echo "   Run ./scripts/build.sh first."
    exit 1
fi

echo "🎮 Launching Geometry Dash via Steam..."
echo "   Mod: $(basename "$GEODE_PACKAGE")"
echo ""

steam steam://rungameid/322170
