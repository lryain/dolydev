#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")"
mkdir -p build

CXX=${CXX:-g++}
CXXFLAGS="-O2 -std=c++17"
INCS="-I../../libs/Doly/include"
LIBS="-L../../libs/Doly/libs -lGpio -lTimer -lgpiod -pthread"

$CXX $CXXFLAGS $INCS tof_en_ctl.cpp -o build/tof_en_ctl $LIBS

echo "Built: build/tof_en_ctl"
