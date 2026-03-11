cmake_minimum_required(VERSION 3.16)
project(doly_audio_player LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -O2")

find_package(Threads REQUIRED)
find_package(PkgConfig REQUIRED)
find_package(yaml-cpp REQUIRED)
find_package(nlohmann_json REQUIRED)

pkg_check_modules(ZMQ REQUIRED libzmq)

add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../capture_control ${CMAKE_BINARY_DIR}/capture_control)

set(SOURCES
    src/audio_player_service.cpp
    src/config.cpp
    ../../3rd/miniaudio/miniaudio.c
)

add_library(audio_player STATIC ${SOURCES})

target_include_directories(audio_player
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/../../3rd/miniaudio
    PRIVATE
        ${ZMQ_INCLUDE_DIRS}
)

target_link_libraries(audio_player
    PUBLIC
        Threads::Threads
        yaml-cpp
        nlohmann_json::nlohmann_json
    PRIVATE
        ${ZMQ_LIBRARIES}
        ${CMAKE_DL_LIBS}
)

target_link_libraries(audio_player PUBLIC capture_control)

add_executable(audio_player_service apps/audio_player_main.cpp)

target_link_libraries(audio_player_service
    PRIVATE
        audio_player
)

target_include_directories(audio_player_service
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

install(TARGETS audio_player audio_player_service
        RUNTIME DESTINATION bin
        LIBRARY DESTINATION lib
        ARCHIVE DESTINATION lib)

install(DIRECTORY include/ DESTINATION include)