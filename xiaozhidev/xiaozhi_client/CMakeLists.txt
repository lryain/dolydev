cmake_minimum_required(VERSION 3.10)
project(xiaozhi_client)
set(CMAKE_CXX_STANDARD 17)

# Optionally enable ccache launcher to speed up local rebuilds. Default OFF.
option(USE_CCACHE "Enable ccache as compiler launcher if available" OFF)
if(USE_CCACHE)
    find_program(CCACHE_PROGRAM ccache)
    if(CCACHE_PROGRAM)
        message(STATUS "Found ccache: ${CCACHE_PROGRAM} - enabling as compiler launcher")
        # Set launcher for C and C++ compilers so ccache is used automatically
        set(CMAKE_C_COMPILER_LAUNCHER ${CCACHE_PROGRAM} CACHE STRING "C compiler launcher" FORCE)
        set(CMAKE_CXX_COMPILER_LAUNCHER ${CCACHE_PROGRAM} CACHE STRING "CXX compiler launcher" FORCE)
    else()
        message(STATUS "ccache not found in PATH; continuing without ccache")
        set(CMAKE_C_COMPILER_LAUNCHER "" CACHE STRING "C compiler launcher" FORCE)
        set(CMAKE_CXX_COMPILER_LAUNCHER "" CACHE STRING "CXX compiler launcher" FORCE)
    endif()
else()
    message(STATUS "USE_CCACHE=OFF - not using ccache")
    set(CMAKE_C_COMPILER_LAUNCHER "" CACHE STRING "C compiler launcher" FORCE)
    set(CMAKE_CXX_COMPILER_LAUNCHER "" CACHE STRING "CXX compiler launcher" FORCE)
endif()

# 收集源文件
file(GLOB src_src CONFIGURE_DEPENDS *.cc)
file(GLOB src_common CONFIGURE_DEPENDS ../common/*.cc)
file(GLOB src_snd_core CONFIGURE_DEPENDS snd_core/*.cc)

set(bin xiaozhi_client)

# 默认的第三方安装前缀（可在 cmake 时用 -D3rd_root=... 覆盖）
set(3rd_root "/home/pi/dolydev/install" CACHE PATH "Third party install prefix")
# /home/pi/dolydev/install

# 包含项目目录和第三方 include
include_directories(
    ${PROJECT_SOURCE_DIR}/snd_core
    ${PROJECT_SOURCE_DIR}/../common
    ${PROJECT_SOURCE_DIR}/../../libs/drive/include
    ${3rd_root}/include
    ${3rd_root}/include/webrtc-audio-processing-1
    ${CMAKE_CURRENT_SOURCE_DIR}
)

# 使用 pkg-config 查找 opus，并找到线程库
find_package(PkgConfig REQUIRED)
pkg_check_modules(OPUS REQUIRED opus)
find_package(Threads REQUIRED)

# 确保链接器可以在本地 install 下查找库（包括 multi-arch 子目录）
link_directories(${3rd_root}/lib ${3rd_root}/lib/aarch64-linux-gnu)

# 添加可执行文件
add_executable(${bin} ${src_src} ${src_common} ${src_snd_core})

 # Prefer pkg-config for webrtc-audio-processing when available (multi-arch pkgconfig path)
set(PC_PATHS ${3rd_root}/lib/pkgconfig ${3rd_root}/lib/aarch64-linux-gnu/pkgconfig /usr/lib/pkgconfig /usr/local/lib/pkgconfig)
list(JOIN PC_PATHS ":" PC_PATHS_JOINED)
set(ENV{PKG_CONFIG_PATH} "${PC_PATHS_JOINED}:$ENV{PKG_CONFIG_PATH}")

pkg_check_modules(WEBRTC_AP QUIET webrtc-audio-processing-1)
if(WEBRTC_AP_FOUND)
    message(STATUS "Found webrtc-audio-processing via pkg-config: ${WEBRTC_AP_VERSION}")
    # pkg-config may report library dirs (multi-arch), add them to linker search path
    if(DEFINED WEBRTC_AP_LIBRARY_DIRS)
        link_directories(${WEBRTC_AP_LIBRARY_DIRS})
    endif()
    target_include_directories(${bin} PRIVATE ${WEBRTC_AP_INCLUDE_DIRS} ${OPUS_INCLUDE_DIRS} ${3rd_root}/include ${3rd_root}/include/webrtc-audio-processing-1)
    target_link_libraries(${bin} PRIVATE ${WEBRTC_AP_LIBRARIES} ${OPUS_LIBRARIES} Threads::Threads zmq rt asound ssl crypto curl z)
else()
    # fallback: try to find library file directly
    find_library(WEBRTC_AP_LIB
    NAMES webrtc-audio-processing-1 webrtc-audio-processing
        PATHS ${3rd_root}/lib ${CMAKE_SOURCE_DIR}/3rd/webrtc-audio-processing-1.3/build /usr/lib /usr/local/lib
    )
    if(WEBRTC_AP_LIB)
        message(STATUS "Found webrtc-audio-processing library: ${WEBRTC_AP_LIB}")
        target_include_directories(${bin} PRIVATE ${OPUS_INCLUDE_DIRS} ${3rd_root}/include ${3rd_root}/include/webrtc-audio-processing-1)
    target_link_libraries(${bin} PRIVATE ${WEBRTC_AP_LIB} ${OPUS_LIBRARIES} Threads::Threads zmq rt asound ssl crypto curl z)
    else()
        message(STATUS "webrtc-audio-processing library not found under ${3rd_root}/lib or system paths.")
        if(EXISTS ${CMAKE_SOURCE_DIR}/3rd/webrtc-audio-processing-1.3)
            message(STATUS "Found source at ${CMAKE_SOURCE_DIR}/3rd/webrtc-audio-processing-1.3. Try building it: cd 3rd/webrtc-audio-processing-1.3 && meson setup build && meson compile -C build && meson install -C build --destdir=${3rd_root}")
        endif()
        message(FATAL_ERROR "webrtc-audio-processing library not found. Please build and install it to ${3rd_root} and re-run CMake.")
    endif()
endif()

install(TARGETS ${bin} DESTINATION bin)