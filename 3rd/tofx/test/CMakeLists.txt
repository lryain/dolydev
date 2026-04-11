cmake_minimum_required(VERSION 3.16)

project(vl6180_pi_tests LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(VL6180_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/..")
get_filename_component(DOLYDEV_ROOT_DEFAULT "${VL6180_ROOT}/../.." ABSOLUTE)
set(DOLYDEV_ROOT "${DOLYDEV_ROOT_DEFAULT}" CACHE PATH "Path to the dolydev workspace root")
set(SDK_ROOT "/home/pi/DOLY-DIY/SDK" CACHE PATH "Path to the DOLY-DIY SDK root")

set(DOLY_GPIO_INCLUDE_DIR "${DOLYDEV_ROOT}/libs/Doly/include")
set(DOLY_GPIO_LIB_DIR "${DOLYDEV_ROOT}/libs/Doly/libs")
set(SDK_INCLUDE_DIR "${SDK_ROOT}/include")
set(SDK_LIB_DIR "${SDK_ROOT}/lib")

find_library(GPIO_LIB Gpio PATHS "${DOLY_GPIO_LIB_DIR}" REQUIRED NO_DEFAULT_PATH)
find_library(TIMER_LIB Timer PATHS "${DOLY_GPIO_LIB_DIR}" REQUIRED NO_DEFAULT_PATH)

find_package(yaml-cpp REQUIRED)

find_library(DRIVECONTROL_LIB DriveControl PATHS "${SDK_LIB_DIR}" REQUIRED NO_DEFAULT_PATH)
find_library(HELPER_LIB Helper PATHS "${SDK_LIB_DIR}" REQUIRED NO_DEFAULT_PATH)

add_library(vl6180_pi_static STATIC IMPORTED GLOBAL)
set_target_properties(vl6180_pi_static PROPERTIES IMPORTED_LOCATION "${VL6180_ROOT}/libvl6180_pi.a")

add_library(tof_test_common STATIC test_common.cpp)
target_include_directories(tof_test_common
    PUBLIC
        "${VL6180_ROOT}/include"
        "${DOLY_GPIO_INCLUDE_DIR}"
)
target_compile_definitions(tof_test_common
    PRIVATE
        DOLYDEV_ROOT="${DOLYDEV_ROOT}"
        SDK_ROOT="${SDK_ROOT}"
        VL6180_TEST_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
)
target_link_libraries(tof_test_common
    PUBLIC
        vl6180_pi_static
        "${GPIO_LIB}"
        "${TIMER_LIB}"
        yaml-cpp
        pthread
        gpiod
)

function(add_tof_test target_name source_file)
    add_executable(${target_name} "${source_file}")
    target_link_libraries(${target_name} PRIVATE tof_test_common)
endfunction()

add_tof_test(tof_demo tof_demo.cpp)
add_tof_test(tof_simpletest tof_simpletest.cpp)
add_tof_test(tof_continuoustest tof_continuoustest.cpp)
add_tof_test(tof_calibration tof_calibration.cpp)
add_tof_test(tof_alts_cal tof_alts_cal.cpp)
add_tof_test(tof_historytest tof_historytest.cpp)
add_tof_test(tof_perform tof_perform.cpp)

add_executable(tof_cruise tof_cruise.cpp)
target_include_directories(tof_cruise PRIVATE "${SDK_INCLUDE_DIR}")
target_link_libraries(tof_cruise
    PRIVATE
        tof_test_common
        "${DRIVECONTROL_LIB}"
        "${HELPER_LIB}"
)
