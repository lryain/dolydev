cmake_minimum_required(VERSION 3.10)
project(msgbus_cpp LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include)

add_library(msgbus_cpp
    src/message.cpp
    src/publisher.cpp
    src/subscriber.cpp
)

target_include_directories(msgbus_cpp PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)

find_package(Threads REQUIRED)
target_link_libraries(msgbus_cpp PUBLIC Threads::Threads)

add_executable(publish_example examples/publish_example.cpp)
target_link_libraries(publish_example PRIVATE msgbus_cpp Threads::Threads)

add_executable(subscribe_example examples/subscribe_example.cpp)
target_link_libraries(subscribe_example PRIVATE msgbus_cpp Threads::Threads)
