#!/usr/bin/env zsh
#
# Build and run script for Level Song Offset (Geode Mod)
# Combines build + run in one step
#

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"

echo "🔨 Building..."
"$SCRIPT_DIR/build.sh"

echo ""
"$SCRIPT_DIR/run.sh"
