cmake_minimum_required(VERSION 3.16)
project(doly_fan LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Threads REQUIRED)
find_package(PkgConfig REQUIRED)
find_package(nlohmann_json REQUIRED)

# ZeroMQ
pkg_check_modules(ZMQ REQUIRED libzmq)
if(ZMQ_FOUND)
    include_directories(${ZMQ_INCLUDE_DIRS})
    add_definitions(-DHAVE_ZMQ)
endif()

# spdlog and fmt (needed by FanControl SDK)
find_library(SPDLOG_LIB spdlog HINTS /usr/local/lib /usr/lib/aarch64-linux-gnu)
find_library(FMT_LIB fmt HINTS /usr/lib/aarch64-linux-gnu)

# FanControl library from SDK
find_path(FANCONTROL_INCLUDE_DIR FanControl.h HINTS /home/pi/DOLY-DIY/SDK/include)
find_library(FANCONTROL_LIB FanControl HINTS /home/pi/DOLY-DIY/SDK/lib)
if(FANCONTROL_INCLUDE_DIR)
    include_directories(${FANCONTROL_INCLUDE_DIR})
endif()

# Helper library from SDK
find_library(HELPER_LIB Helper HINTS /home/pi/DOLY-DIY/SDK/lib)

add_library(fan STATIC
    src/fan_service_bus.cpp
)

target_include_directories(fan
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/../Doly/include
)

target_link_libraries(fan
    PUBLIC
        Threads::Threads
        ${FANCONTROL_LIB}
        ${HELPER_LIB}
        ${SPDLOG_LIB}
        ${FMT_LIB}
)

add_executable(fan_service
    apps/fan_service_main.cpp
)

target_link_libraries(fan_service
    PRIVATE
        fan
        ${ZMQ_LIBRARIES}
        nlohmann_json::nlohmann_json
        ${FANCONTROL_LIB}
        ${HELPER_LIB}
        ${SPDLOG_LIB}
        ${FMT_LIB}
)

install(TARGETS fan fan_service RUNTIME DESTINATION bin LIBRARY DESTINATION lib ARCHIVE DESTINATION lib)
