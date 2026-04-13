cmake_minimum_required(VERSION 3.5)
project(test_cam)

set(CMAKE_CXX_STANDARD 11)
find_package(OpenCV REQUIRED)

add_executable(test_cam test_cam.cpp)
if(OpenCV_FOUND)
    message(STATUS "OpenCV include dirs: ${OpenCV_INCLUDE_DIRS}")
    message(STATUS "OpenCV libs: ${OpenCV_LIBS}")
    target_include_directories(test_cam PRIVATE ${OpenCV_INCLUDE_DIRS})
    target_link_libraries(test_cam ${OpenCV_LIBS})
endif()

# small GUI init test
add_executable(gui_test gui_test.cpp)
if(OpenCV_FOUND)
    target_include_directories(gui_test PRIVATE ${OpenCV_INCLUDE_DIRS})
    target_link_libraries(gui_test ${OpenCV_LIBS})
endif()
