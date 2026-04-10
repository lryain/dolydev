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
