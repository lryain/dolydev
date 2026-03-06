#!/bin/bash

# Fan Service Build Script (cmake based)
# Compiles the fan temperature control service using CMake

set -e

SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
PROJECT_ROOT="$SCRIPT_DIR/.."
cd "$PROJECT_ROOT"

echo "=== Building Fan Temperature Control Service via CMake ==="

BUILD_DIR="build"

# Create and enter build directory
mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"

# run cmake and build
cmake ..
make -j$(nproc)

echo "Build completed successfully. Executables are in $(pwd)/"

# note: binaries installed with 'make install' will go to CMAKE_INSTALL_PREFIX (default /usr/local)
