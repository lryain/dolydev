cmake_minimum_required(VERSION 3.18)
project(doly_servo_py LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

find_package(pybind11 REQUIRED)
find_package(Threads REQUIRED)

set(DOLY_SDK_ROOT "/home/pi/DOLY-DIY/SDK" CACHE PATH "Path to DOLY SDK root")

pybind11_add_module(doly_servo bindings.cpp)

target_include_directories(doly_servo PRIVATE
    ${DOLY_SDK_ROOT}/include
)

target_link_directories(doly_servo PRIVATE
    ${DOLY_SDK_ROOT}/lib
)

target_link_libraries(doly_servo PRIVATE
    ServoControl
    Threads::Threads
)

set_target_properties(doly_servo PROPERTIES
    BUILD_RPATH "${DOLY_SDK_ROOT}/lib"
    INSTALL_RPATH "${DOLY_SDK_ROOT}/lib"
)

install(TARGETS doly_servo LIBRARY DESTINATION .)