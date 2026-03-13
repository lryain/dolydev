cmake_minimum_required(VERSION 3.16)
project(DolyWidgetService LANGUAGES CXX)

# default OpenCV location override (mirror other modules)
if(NOT DEFINED OpenCV_DIR)
    set(OpenCV_DIR "/.doly/libs/opencv/lib/cmake/opencv4")
endif()

find_package(Threads REQUIRED)
find_package(PNG REQUIRED)
find_package(OpenCV REQUIRED)
find_package(CURL REQUIRED)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ====== 路径定义 ======
set(EYE_ENGINE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../EyeEngine")
set(DOLY_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../Doly")
# set(MSGBUS_CPP_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../bus")
set(PROJECT_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../..")

# ====== EyeEngine 静态库（复用已编译的 widget 代码）======
set(EYE_ENGINE_LIB "${DOLY_DIR}/libs/libdoly_eye_engine.a")
set(DOLY_LIBLCDCONTROL "${DOLY_DIR}/libs/libLcdControl.a")

# ====== 头文件搜索路径 ======
include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${EYE_ENGINE_DIR}/include
    ${DOLY_DIR}/include
    # ${MSGBUS_CPP_DIR}/include
    ${PROJECT_ROOT}/libs/coms/ipc/include
    ${PROJECT_ROOT}/libs/coms/video/include
    ${PROJECT_ROOT}/libs/coms/video_stream/include
    ${PROJECT_ROOT}/libs/coms/video_consumer/include
    ${PROJECT_ROOT}/libs/msgbus_cpp/include
)

# ====== 链接库搜索路径 ======
link_directories(
    ${PROJECT_ROOT}/libs/coms/build
    ${PROJECT_ROOT}/libs/coms/build/video
    ${PROJECT_ROOT}/libs/coms/build/video_stream
    ${PROJECT_ROOT}/libs/coms/build/video_consumer
)

# ====== Widget Service 可执行文件 ======
add_executable(widget_service
    src/widget_service.cpp
)

target_link_libraries(widget_service
    PRIVATE
        ${EYE_ENGINE_LIB}
        ${DOLY_LIBLCDCONTROL}
        PNG::PNG
        ${OpenCV_LIBS}
        nora_coms_ipc nora_coms_video nora_coms_video_stream nora_coms_video_consumer nora_coms_msgbus_bridge
        zmq
        Threads::Threads
        CURL::libcurl
)

target_compile_definitions(widget_service PRIVATE
    DOLY_WIDGET_SERVICE_VERSION="0.1.0"
)
