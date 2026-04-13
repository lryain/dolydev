
include(FetchContent)

# 优先使用系统安装的 GTest，避免 FetchContent 在较旧工程的 cmake_minimum_required 约束下失败。
find_package(GTest QUIET)
if(NOT GTest_FOUND)
  message(WARNING "GTest not found and FetchContent download is disabled for offline builds. Skipping tests. Install system package (e.g. libgtest-dev) to enable BUILD_TESTS.")
  return()
endif()

enable_testing()

set(TESTS_DIR ${CMAKE_SOURCE_DIR}/tests/)

##################### TEST PARALLEL VIDEO CAPTURE #####################
ADD_EXECUTABLE(gtest_parallel_video_capture ${TESTS_DIR}/ParallelVideoCapture/gtest_parallel_video_capture.cpp)

if(GTest_FOUND)
  TARGET_LINK_LIBRARIES(gtest_parallel_video_capture ParallelVideoCapture ${OpenCV_LIBS} GTest::gtest_main)
else()
  TARGET_LINK_LIBRARIES(gtest_parallel_video_capture ParallelVideoCapture ${OpenCV_LIBS} gtest_main)
endif()

TARGET_INCLUDE_DIRECTORIES(gtest_parallel_video_capture PUBLIC  ${CMAKE_SOURCE_DIR}/src/ParallelVideoCapture)
#######################################################################

##################### TEST FRAME CONTEXT #####################
ADD_EXECUTABLE(gtest_frame_context ${TESTS_DIR}/test_frame_context.cpp)

if(GTest_FOUND)
  TARGET_LINK_LIBRARIES(gtest_frame_context ${OpenCV_LIBS} GTest::gtest_main)
else()
  TARGET_LINK_LIBRARIES(gtest_frame_context ${OpenCV_LIBS} gtest_main)
endif()

TARGET_INCLUDE_DIRECTORIES(gtest_frame_context PUBLIC  ${CMAKE_SOURCE_DIR}/include)

add_test(NAME FrameContextTest COMMAND gtest_frame_context)
##############################################################

##################### TEST CONFIG MANAGER #####################
ADD_EXECUTABLE(gtest_config_manager ${TESTS_DIR}/test_config_manager.cpp)

if(GTest_FOUND)
  TARGET_LINK_LIBRARIES(gtest_config_manager facedetect ${OpenCV_LIBS} GTest::gtest_main)
else()
  TARGET_LINK_LIBRARIES(gtest_config_manager facedetect ${OpenCV_LIBS} gtest_main)
endif()

TARGET_INCLUDE_DIRECTORIES(gtest_config_manager PUBLIC  ${CMAKE_SOURCE_DIR}/include ${CMAKE_SOURCE_DIR}/src)

add_test(NAME ConfigManagerTest COMMAND gtest_config_manager)
#################################################################

##################### TEST DEPENDENCY MANAGER #####################
ADD_EXECUTABLE(gtest_dependency_manager ${TESTS_DIR}/test_dependency_manager.cpp)

if(GTest_FOUND)
  TARGET_LINK_LIBRARIES(gtest_dependency_manager facedetect ${OpenCV_LIBS} GTest::gtest_main)
else()
  TARGET_LINK_LIBRARIES(gtest_dependency_manager facedetect ${OpenCV_LIBS} gtest_main)
endif()

TARGET_INCLUDE_DIRECTORIES(gtest_dependency_manager PUBLIC  ${CMAKE_SOURCE_DIR}/include ${CMAKE_SOURCE_DIR}/src)

add_test(NAME DependencyManagerTest COMMAND gtest_dependency_manager)
#####################################################################

##################### TEST MODULE LIFECYCLE MANAGER #####################
ADD_EXECUTABLE(gtest_module_lifecycle_manager ${TESTS_DIR}/test_module_lifecycle_manager.cpp)

if(GTest_FOUND)
  TARGET_LINK_LIBRARIES(gtest_module_lifecycle_manager facedetect ${OpenCV_LIBS} GTest::gtest_main)
else()
  TARGET_LINK_LIBRARIES(gtest_module_lifecycle_manager facedetect ${OpenCV_LIBS} gtest_main)
endif()

TARGET_INCLUDE_DIRECTORIES(gtest_module_lifecycle_manager PUBLIC  ${CMAKE_SOURCE_DIR}/include ${CMAKE_SOURCE_DIR}/src)

add_test(NAME ModuleLifecycleManagerTest COMMAND gtest_module_lifecycle_manager)
########################################################################

##################### TEST MODULE MANAGER (M2) #####################
ADD_EXECUTABLE(gtest_module_manager ${TESTS_DIR}/test_module_manager.cpp)

if(GTest_FOUND)
  TARGET_LINK_LIBRARIES(gtest_module_manager facedetect ${OpenCV_LIBS} GTest::gtest_main)
else()
  TARGET_LINK_LIBRARIES(gtest_module_manager facedetect ${OpenCV_LIBS} gtest_main)
endif()

TARGET_INCLUDE_DIRECTORIES(gtest_module_manager PUBLIC  ${CMAKE_SOURCE_DIR}/include ${CMAKE_SOURCE_DIR}/src)

add_test(NAME ModuleManagerTest COMMAND gtest_module_manager)
####################################################################
