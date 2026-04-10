cmake_minimum_required(VERSION 3.16)
project(example LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-error=missing-field-initializers")

# 让 spdlog 使用外部 fmt（Vivoka 捆绑的 fmt），避免双重定义冲突
add_compile_definitions(SPDLOG_FMT_EXTERNAL)

add_executable(example
    src/main.cpp
    src/test_asr.cpp
    src/test_tts.cpp
    src/test_nlu.cpp
    src/test_audio.cpp
)

# Add include dirs - IMPORTANT: Add VIVOKA includes BEFORE system includes
target_include_directories(example PRIVATE
  /.doly/libs/vivoka/include
  /home/pi/DOLY-DIY/SDK/include
  /usr/local/include
)

# Add link dirs
target_link_directories(example PRIVATE
  /.doly/libs/vivoka/lib
  /home/pi/DOLY-DIY/SDK/lib
  /usr/local/lib
)

# Link libraries
target_link_libraries(example PRIVATE
  # Vivoka VSDK - concrete ASR implementation
  vsdk-csdk-asr
  vsdk-csdk-core
  vsdk
  vsdk-exception
  nlohmann_json_schema_validator
  # System libraries
  pthread
  spdlog
  dl
)
