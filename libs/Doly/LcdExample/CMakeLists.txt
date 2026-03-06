cmake_minimum_required(VERSION 3.10)
project(LcdExample LANGUAGES CXX)

# Allow overriding the location of the Doly SDK (headers/libs)
set(DOLY_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../Doly" CACHE PATH "Path to Doly (contains include/ and libs/)")

message(STATUS "DOLY_ROOT=${DOLY_ROOT}")

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include_directories(${DOLY_ROOT}/include)

# Helper to add an executable target that links to libLcdControl, libpng and zlib when available
function(add_lcd_exe name src)
    add_executable(${name} ${src})
    target_include_directories(${name} PRIVATE ${DOLY_ROOT}/include)

    # Prefer static archive in Doly/libs if present, otherwise let system find -lLcdControl
    set(DOLY_LIB_ARCHIVE "${DOLY_ROOT}/libs/libLcdControl.a")
    if(EXISTS ${DOLY_LIB_ARCHIVE})
        message(STATUS "Linking ${name} with ${DOLY_LIB_ARCHIVE}")
        target_link_libraries(${name} PRIVATE ${DOLY_LIB_ARCHIVE})
    else()
        message(STATUS "libLcdControl.a not found in ${DOLY_ROOT}/libs, linking by name -lLcdControl")
        target_link_libraries(${name} PRIVATE LcdControl)
    endif()

    find_package(PNG QUIET)
    if(PNG_FOUND)
        target_include_directories(${name} PRIVATE ${PNG_INCLUDE_DIRS})
        target_link_libraries(${name} PRIVATE ${PNG_LIBRARIES})
    else()
        # fall back to system libs when find_package not available
        target_link_libraries(${name} PRIVATE png)
    endif()

    find_package(ZLIB QUIET)
    if(ZLIB_FOUND)
        target_include_directories(${name} PRIVATE ${ZLIB_INCLUDE_DIRS})
        target_link_libraries(${name} PRIVATE ${ZLIB_LIBRARIES})
    else()
        target_link_libraries(${name} PRIVATE z)
    endif()
    # Link threading library (pthreads) and set sane compile flags
    find_package(Threads REQUIRED)
    target_link_libraries(${name} PRIVATE Threads::Threads)

    target_compile_definitions(${name} PRIVATE _CRT_SECURE_NO_WARNINGS)
    target_compile_options(${name} PRIVATE -Wall)
endfunction()

# Define targets
add_lcd_exe(anim anim.cpp)
add_lcd_exe(example_main main.cpp)

# pylcd.cpp does not contain a main() entrypoint; build it as a shared library for embedding (Python wrapper etc.)
add_library(pylcd SHARED pylcd.cpp animation_player.cpp eye_frames.cpp utils.cpp)
target_include_directories(pylcd PRIVATE ${DOLY_ROOT}/include)

# link same dependencies as executables
set(DOLY_LIB_ARCHIVE "${DOLY_ROOT}/libs/libLcdControl.a")
if(EXISTS ${DOLY_LIB_ARCHIVE})
    target_link_libraries(pylcd PRIVATE ${DOLY_LIB_ARCHIVE})
else()
    target_link_libraries(pylcd PRIVATE LcdControl)
endif()

if(PNG_FOUND)
    target_include_directories(pylcd PRIVATE ${PNG_INCLUDE_DIRS})
    target_link_libraries(pylcd PRIVATE ${PNG_LIBRARIES})
else()
    target_link_libraries(pylcd PRIVATE png)
endif()

if(ZLIB_FOUND)
    target_include_directories(pylcd PRIVATE ${ZLIB_INCLUDE_DIRS})
    target_link_libraries(pylcd PRIVATE ${ZLIB_LIBRARIES})
else()
    target_link_libraries(pylcd PRIVATE z)
endif()

target_link_libraries(pylcd PRIVATE Threads::Threads)
target_compile_definitions(pylcd PRIVATE _CRT_SECURE_NO_WARNINGS)
target_compile_options(pylcd PRIVATE -Wall)

## Link pylcd into the small test executables so they can call into the shared implementation
if(TARGET pylcd)
    target_link_libraries(anim PRIVATE pylcd)
    target_link_libraries(example_main PRIVATE pylcd)
endif()

message(STATUS "Configured LcdExample targets: anim, pylcd, example_main")
