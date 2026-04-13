#!/bin/bash
set -e

# Paths
SOURCE_DIR="/home/pi/dolydev/libs/serial"
BUILD_DIR="${SOURCE_DIR}/build_sdk"
SDK_ROOT="/home/pi/DOLY-DIY/SDK"
SDK_LIB_DIR="${SDK_ROOT}/lib"
SDK_INC_DIR="${SDK_ROOT}/include"
EXAMPLE_CPP_DIR="${SDK_ROOT}/examples/cpp/SerialControl"
EXAMPLE_PY_DIR="${SDK_ROOT}/examples/python/SerialControl"

echo "=== Publishing SerialControl SDK ==="

# 1. Build and install library
mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc) SerialControl

# Copy library
echo "Installing library to ${SDK_LIB_DIR}..."
mkdir -p "${SDK_LIB_DIR}"
cp -v libSerialControl.so* "${SDK_LIB_DIR}/"

# Copy headers
echo "Installing headers to ${SDK_INC_DIR}..."
mkdir -p "${SDK_INC_DIR}/serial"
cp -v "${SOURCE_DIR}/include/serial/"*.hpp "${SDK_INC_DIR}/serial/"

# 2. Create C++ Example if not exists
echo "Creating/Updating C++ Example..."
mkdir -p "${EXAMPLE_CPP_DIR}/src"

cat << 'EOF' > "${EXAMPLE_CPP_DIR}/main.cpp"
#include <serial/serial_service.hpp>
#include <iostream>
#include <thread>
#include <chrono>

int main() {
    doly::serial::SerialService service;
    doly::serial::SerialConfig cfg;
    cfg.device = "/dev/ttyUSB0";
    cfg.baud = 115200;
    cfg.use_simulator = false;

    std::cout << "Initializing SerialControl..." << std::endl;
    if (!service.init(cfg)) {
        std::cerr << "Failed to init serial" << std::endl;
        return 1;
    }

    service.set_handler([](uint8_t byte) {
        std::cout << "Received byte: 0x" << std::hex << (int)byte << std::dec << std::endl;
    });

    std::cout << "Starting serial read loop (5 seconds test)..." << std::endl;
    service.start();

    std::this_thread::sleep_for(std::chrono::seconds(5));

    std::cout << "Stopping..." << std::endl;
    service.stop();
    return 0;
}
EOF

cat << 'EOF' > "${EXAMPLE_CPP_DIR}/CMakeLists.txt"
cmake_minimum_required(VERSION 3.16)
project(SerialControlExample LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(example main.cpp)

target_include_directories(example PRIVATE
    /home/pi/DOLY-DIY/SDK/include
)

target_link_directories(example PRIVATE
    /home/pi/DOLY-DIY/SDK/lib
)

target_link_libraries(example PRIVATE
    SerialControl
    pthread
)
EOF

# 3. Create Python Binding if not exists
echo "Creating/Updating Python Example..."
mkdir -p "${EXAMPLE_PY_DIR}/source"

# We'll use pybind11 for bindings
cat << 'EOF' > "${EXAMPLE_PY_DIR}/source/bindings.cpp"
#include <pybind11/pybind11.h>
#include <pybind11/functional.h>
#include <pybind11/stl.h>
#include <serial/serial_service.hpp>

namespace py = pybind11;
using namespace doly::serial;

PYBIND11_MODULE(SerialControl, m) {
    py::class_<SerialConfig>(m, "SerialConfig")
        .def(py::init<>())
        .def_readwrite("device", &SerialConfig::device)
        .def_readwrite("baud", &SerialConfig::baud)
        .def_readwrite("use_simulator", &SerialConfig::use_simulator)
        .def_readwrite("sim_file", &SerialConfig::sim_file);

    py::class_<SerialService>(m, "SerialService")
        .def(py::init<>())
        .def("init", &SerialService::init)
        .def("start", &SerialService::start)
        .def("stop", &SerialService::stop)
        .def("set_handler", &SerialService::set_handler);
}
EOF

cat << 'EOF' > "${EXAMPLE_PY_DIR}/source/CMakeLists.txt"
cmake_minimum_required(VERSION 3.16)
project(SerialControlPython BINDING)

find_package(pybind11 REQUIRED)

pybind11_add_module(SerialControl bindings.cpp)

target_include_directories(SerialControl PRIVATE
    /home/pi/DOLY-DIY/SDK/include
)

target_link_directories(SerialControl PRIVATE
    /home/pi/DOLY-DIY/SDK/lib
)

target_link_libraries(SerialControl PRIVATE
    SerialControl
)
EOF

cat << 'EOF' > "${EXAMPLE_PY_DIR}/source/pyproject.toml"
[build-system]
requires = ["setuptools", "wheel", "pybind11", "cmake"]
build-backend = "setuptools.build_meta"

[project]
name = "SerialControl"
version = "1.0.0"
EOF

cat << 'EOF' > "${EXAMPLE_PY_DIR}/example.py"
import SerialControl
import time

def on_byte(byte):
    print(f"Python received byte: 0x{byte:02x}")

def main():
    cfg = SerialControl.SerialConfig()
    cfg.device = "/dev/ttyUSB0"
    cfg.baud = 115200
    
    service = SerialControl.SerialService()
    if not service.init(cfg):
        print("Failed to init")
        return

    service.set_handler(on_byte)
    
    print("Starting SerialControl Python test (5 seconds)...")
    service.start()
    
    time.sleep(5)
    
    print("Stopping...")
    service.stop()

if __name__ == "__main__":
    main()
EOF

echo "=== SDK Published successfully to ${SDK_ROOT} ==="
