cmake_minimum_required(VERSION 3.16)
project(example LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(example main.cpp)

# Add include dirs
target_include_directories(example PRIVATE
  /home/pi/DOLY-DIY/SDK/include
  /usr/local/include            # spdlog headers (built from source)
  /.doly/libs/opencv/include/opencv4   # robot-provided OpenCV headers
  /usr/include/libcamera/             # camera headers (system or robot)
)

# Add link dirs
target_link_directories(example PRIVATE
	/home/pi/DOLY-DIY/SDK/lib
  /usr/local/lib                # spdlog library location
  /.doly/libs/opencv/lib       # robot-provided OpenCV libs
  # system libraries (libcamera) are found automatically
)

# Link library
target_link_libraries(example PRIVATE
  LCCV
  camera
  camera-base
  spdlog
  opencv_core
  opencv_highgui
  opencv_imgcodecs
  opencv_imgproc
  pthread
)
