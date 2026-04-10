cmake_minimum_required(VERSION 3.16)
project(SerialControlPython LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(pybind11 REQUIRED)

# Use dynamic linking to SerialControl
pybind11_add_module(SerialControlPy bindings.cpp)

target_include_directories(SerialControlPy PRIVATE
    /home/pi/DOLY-DIY/SDK/include
)

target_link_directories(SerialControlPy PRIVATE
    /home/pi/DOLY-DIY/SDK/lib
)

target_link_libraries(SerialControlPy PRIVATE
    SerialControl
)

set_target_properties(SerialControlPy PROPERTIES OUTPUT_NAME SerialControl)
