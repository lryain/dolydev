cmake_minimum_required(VERSION 3.16)
project(doly_autocharge VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -O2 -fPIC")

option(BUILD_TESTS "Build autocharge tests" ON)

find_package(yaml-cpp REQUIRED)

set(DOLY_SDK_ROOT "/home/pi/DOLY-DIY/SDK" CACHE PATH "Path to DOLY SDK")
set(DOLYDEV_ROOT "/home/pi/dolydev" CACHE PATH "Path to dolydev workspace")
set(OPENCV_ROOT "/.doly/libs/opencv" CACHE PATH "Path to OpenCV bundle")
set(VL6180_ROOT "/home/pi/dolydev/3rd/vl6180_pi" CACHE PATH "Path to cpp VL6180 library")

add_library(doly_autocharge STATIC
    src/config_loader.cpp
    src/marker_detector.cpp
    src/docking_planner.cpp
    src/power_monitor.cpp
    src/tof_monitor.cpp
    src/debug_renderer.cpp
    src/autocharge_service.cpp
)

target_include_directories(doly_autocharge
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${DOLYDEV_ROOT}/libs/drive/include
        ${OPENCV_ROOT}/include/opencv4
    PRIVATE
        ${DOLY_SDK_ROOT}/include
        ${DOLYDEV_ROOT}/libs/Doly/include
        ${VL6180_ROOT}/include
        /usr/include/libcamera
)

target_link_directories(doly_autocharge
    PUBLIC
        ${OPENCV_ROOT}/lib
        ${VL6180_ROOT}
    PRIVATE
        ${DOLY_SDK_ROOT}/lib
        /usr/local/lib
)

target_link_libraries(doly_autocharge
    PUBLIC
        rt
        yaml-cpp
        vl6180_pi
        opencv_core
        opencv_imgproc
        opencv_imgcodecs
        opencv_objdetect
        opencv_calib3d
)

add_executable(autocharge_service
    src/autocharge_service_main.cpp
)

target_include_directories(autocharge_service PRIVATE
    ${DOLY_SDK_ROOT}/include
    ${OPENCV_ROOT}/include/opencv4
    ${DOLYDEV_ROOT}/libs/Doly/include
    ${VL6180_ROOT}/include
    /usr/include/libcamera
)

target_link_directories(autocharge_service PRIVATE
    ${DOLY_SDK_ROOT}/lib
    ${OPENCV_ROOT}/lib
    ${VL6180_ROOT}
    /usr/local/lib
)

target_link_libraries(autocharge_service PRIVATE
    doly_autocharge
    LCCV
    DriveControl
    Helper
    LcdControl
    vl6180_pi
    camera
    camera-base
    opencv_core
    opencv_imgproc
    opencv_imgcodecs
    opencv_objdetect
    opencv_calib3d
    pthread
)

# Ensure the autocharge_service executable prefers the SDK lib directory at runtime
set_target_properties(autocharge_service PROPERTIES
    BUILD_WITH_INSTALL_RPATH TRUE
    BUILD_RPATH "${DOLY_SDK_ROOT}/lib;${VL6180_ROOT};$ORIGIN"
    INSTALL_RPATH "${DOLY_SDK_ROOT}/lib;${VL6180_ROOT}"
)

add_executable(autocharge_marker_tool
    src/marker_tool_main.cpp
)

target_include_directories(autocharge_marker_tool PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${OPENCV_ROOT}/include/opencv4
)

target_link_directories(autocharge_marker_tool PRIVATE
    ${OPENCV_ROOT}/lib
)

target_link_libraries(autocharge_marker_tool PRIVATE
    opencv_core
    opencv_imgproc
    opencv_imgcodecs
    opencv_objdetect
)

if(BUILD_TESTS)
    enable_testing()

    add_executable(test_docking_planner
        tests/test_docking_planner.cpp
    )

    target_include_directories(test_docking_planner PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${OPENCV_ROOT}/include/opencv4
        ${VL6180_ROOT}/include
    )

    target_link_libraries(test_docking_planner PRIVATE
        doly_autocharge
        vl6180_pi
    )

    add_executable(test_marker_detector
        tests/test_marker_detector.cpp
    )

    target_include_directories(test_marker_detector PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${OPENCV_ROOT}/include/opencv4
    )

    target_link_libraries(test_marker_detector PRIVATE
        doly_autocharge
        opencv_core
        opencv_imgproc
        opencv_objdetect
        opencv_imgcodecs
    )

    add_test(NAME marker_detector_test COMMAND test_marker_detector)

    add_test(NAME docking_planner_test COMMAND test_docking_planner)
endif()
