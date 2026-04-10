cmake_minimum_required(VERSION 3.16)
project(example LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(example main.cpp)

# Add include dirs
target_include_directories(example PRIVATE
  /home/pi/DOLY-DIY/SDK/include
  /usr/local/include            # spdlog headers (built from source)
)

# Add link dirs
target_link_directories(example PRIVATE
	/home/pi/DOLY-DIY/SDK/lib
  /usr/local/lib                # spdlog library location
)

# Link library
target_link_libraries(example PRIVATE
  SoundControl
	TtsControl
	Helper
  spdlog
  pthread
)
