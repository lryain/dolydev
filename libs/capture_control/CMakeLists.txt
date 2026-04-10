cmake_minimum_required(VERSION 3.16)
project(capture_control C CXX)

add_library(capture_control STATIC
    src/capture_control.cpp
)

target_include_directories(capture_control
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_link_libraries(capture_control PUBLIC rt)

install(TARGETS capture_control ARCHIVE DESTINATION lib)
install(DIRECTORY include/ DESTINATION include)
