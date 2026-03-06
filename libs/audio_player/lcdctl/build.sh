#!/usr/bin/env bash
set -euo pipefail

# Simple build helper for the LcdExample folder using CMake.
# Usage:
#   ./build.sh            -> out-of-source build in build/ using bundled Doly at ../Doly
#   DOLY_ROOT=/path/to/Doly ./build.sh

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="$SCRIPT_DIR/build"

: ${DOLY_ROOT:="$SCRIPT_DIR/../Doly"}

echo "Building LcdExample"
echo "SOURCE: $SCRIPT_DIR"
echo "BUILD:  $BUILD_DIR"
echo "DOLY_ROOT: $DOLY_ROOT"

mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"

# Configure
cmake -DCMAKE_BUILD_TYPE=Release -DDOLY_ROOT="$DOLY_ROOT" "$SCRIPT_DIR"

# Build
cmake --build . -- -j$(nproc)

echo "Build finished. Binaries are in: $BUILD_DIR"

echo "Available targets:"
ls -1 "$BUILD_DIR" | sed -n '1,200p'
