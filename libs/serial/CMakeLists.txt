cmake_minimum_required(VERSION 3.16)
project(doly_serial LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Threads REQUIRED)
find_package(PkgConfig REQUIRED)
find_package(yaml-cpp QUIET)
if(NOT yaml-cpp_FOUND)
    message(STATUS "yaml-cpp not found, skipping service build (library only)")
endif()

find_package(nlohmann_json QUIET)
pkg_check_modules(ZMQ QUIET libzmq)

add_library(SerialControl SHARED
    src/serial_service.cpp
)

target_include_directories(SerialControl
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

target_link_libraries(SerialControl
    PUBLIC
        Threads::Threads
)

set_target_properties(SerialControl PROPERTIES
    VERSION 1.0.0
    SOVERSION 1
    OUTPUT_NAME SerialControl
)

if(yaml-cpp_FOUND AND nlohmann_json_FOUND AND ZMQ_FOUND)
    add_executable(serial_service apps/serial_service_main.cpp)

    target_link_libraries(serial_service
        PRIVATE
            SerialControl
            ${ZMQ_LIBRARIES}
            nlohmann_json::nlohmann_json
            yaml-cpp
    )
    install(TARGETS serial_service RUNTIME DESTINATION bin)
endif()

install(TARGETS SerialControl
    LIBRARY DESTINATION lib 
    ARCHIVE DESTINATION lib
)

install(DIRECTORY include/ DESTINATION include)
