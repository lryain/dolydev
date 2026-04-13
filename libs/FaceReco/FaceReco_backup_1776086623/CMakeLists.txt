CMAKE_MINIMUM_REQUIRED(VERSION 3.6)

PROJECT(LiveFaceReco)

list( APPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/cmake)

# default OpenCV location override (mirror other modules)
if(NOT DEFINED OpenCV_DIR)
    set(OpenCV_DIR "/.doly/libs/opencv/lib/cmake/opencv4")
endif()

find_package(OpenCV REQUIRED)
find_package(nlohmann_json REQUIRED)
find_package(yaml-cpp REQUIRED)

SET(CMAKE_CXX_STANDARD 17)
SET(CMAKE_CXX_STANDARD_REQUIRED ON)
SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O3")
SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -march=native")
SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}  -ldl -lpthread")
SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -lstdc++fs")

# 🆕 Nora Coms IPC 和视频流库集成
LINK_DIRECTORIES(${HOME_TVM}/build)
LINK_DIRECTORIES(/home/pi/dolydev/libslibs/coms/build)
LINK_DIRECTORIES(/home/pi/dolydev/libslibs/coms/build/video_stream)
LINK_DIRECTORIES(/home/pi/dolydev/SDK/lib)
LINK_DIRECTORIES(/usr/local/lib)

INCLUDE_DIRECTORIES(${OpenCV_INCLUDE_DIRS}
			${PROJECT_SOURCE_DIR}/include
			/home/pi/dolydev/SDK/include
			/usr/include/libcamera
			/home/pi/dolydev/libslibs/coms/ipc/include
			/home/pi/dolydev/libslibs/coms/video/include
			/home/pi/dolydev/libslibs/coms/video_stream/include)

INCLUDE_DIRECTORIES(${CMAKE_CURRENT_LIST_DIR}/src)
INCLUDE_DIRECTORIES(${CMAKE_CURRENT_LIST_DIR}/include/ncnn)

# try to find ncnn library installed under /usr/local/lib/ncnn or /usr/local/lib
set(NCNN_SEARCH_DIRS /usr/local/lib/ncnn /usr/local/lib)
find_library(NCNN_LIB NAMES ncnn PATHS ${NCNN_SEARCH_DIRS} NO_DEFAULT_PATH)
if(NCNN_LIB)
	message(STATUS "Found ncnn library: ${NCNN_LIB}")
else()
	message(WARNING "ncnn library not found in ${NCNN_SEARCH_DIRS}; falling back to plain 'ncnn' (you may need to run ldconfig or adjust NCNN_SEARCH_DIRS)")
	set(NCNN_LIB ncnn)
endif()

find_package(OpenMP REQUIRED)
if(OPENMP_FOUND)
    message("OPENMP FOUND")
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${OpenMP_C_FLAGS}")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${OpenMP_CXX_FLAGS}")
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} ${OpenMP_EXE_LINKER_FLAGS}")
endif()

ADD_LIBRARY(facedetect STATIC
			include/FacePreprocess.h 
						
			src/livefacereco.hpp
			src/livefacereco.cpp
			src/config.cpp
			src/live.cpp
			src/live.h
			src/arcface.cpp
			src/arcface.h
			src/mtcnn_new.cpp
			src/mtcnn_new.h
			src/DatasetHandler/image_dataset_handler.hpp
			src/vision/ConfigManager.cpp
			src/vision/DependencyManager.cpp
			src/vision/ModuleLifecycleManager.cpp
			src/vision/noop_module.cpp
			src/vision/module_manager.cpp
			src/vision_service.cpp
			src/face_database.cpp
			src/capture_module.cpp
			src/runtime_control.cpp
			src/vision_bus_bridge.cpp
			src/yaml_config.cpp

)

# 🆕 为 facedetect 库设置目标级别的 include 目录
TARGET_INCLUDE_DIRECTORIES(facedetect PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../coms/ipc/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../coms/video_stream/include
)

add_subdirectory(src)

if(${BUILD_TESTS})
	include(${CMAKE_MODULE_PATH}/Tests.cmake)
endif()
#LINK_DIRECTORIES(${CMAKE_CURRENT_LIST_DIR}/include/libs/RaspberryPi4B)
ADD_EXECUTABLE(LiveFaceReco src/main.cpp)

TARGET_LINK_LIBRARIES(LiveFaceReco ${OpenCV_LIBS} facedetect ${NCNN_LIB} pthread stdc++fs dl
	nora_coms_ipc nora_coms_video nora_coms_video_stream zmq nlohmann_json::nlohmann_json yaml-cpp
	LCCV camera camera-base spdlog)
TARGET_LINK_LIBRARIES(facedetect ${OpenCV_LIBS} ${NCNN_LIB} nlohmann_json::nlohmann_json yaml-cpp)
TARGET_INCLUDE_DIRECTORIES(LiveFaceReco PRIVATE ${OpenCV_INCLUDE_DIRS})
