#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="$SCRIPT_DIR/build"
INSTALL_DIR="$SCRIPT_DIR/install"

echo "[INFO] build: using cmake to build in $BUILD_DIR"
mkdir -p "$BUILD_DIR"
cmake -S "$SCRIPT_DIR" -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=Release
cmake --build "$BUILD_DIR" -j$(nproc 2>/dev/null || echo 1)

# copy binary to install
mkdir -p "$INSTALL_DIR"
if [ -f "$BUILD_DIR/audio_player_service" ]; then
    cp "$BUILD_DIR/audio_player_service" "$INSTALL_DIR/audio_player_service"
    chmod +x "$INSTALL_DIR/audio_player_service"
    echo "[SUCCESS] build: copied to $INSTALL_DIR/audio_player_service"
else
    echo "[WARNING] build: artifact not found: $BUILD_DIR/audio_player_service"
    exit 2
fi
