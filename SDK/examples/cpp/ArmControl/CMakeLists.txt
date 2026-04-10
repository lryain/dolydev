cmake_minimum_required(VERSION 3.10)
project(arm_example)
set(CMAKE_CXX_STANDARD 14)

include_directories(/home/pi/DOLY-DIY/SDK/include)
include_directories(/home/pi/dolydev/libs/servo_drive/include)
include_directories(/home/pi/dolydev/libs/Doly/include)

add_executable(example main.cpp)

# 使用 servo_drive 编译的库和 Doly 的静态库
target_link_options(example PRIVATE -Wl,--start-group)
target_link_libraries(example 
    /home/pi/dolydev/libs/servo_drive/build/libdoly_arm_control.a
    /home/pi/dolydev/libs/servo_drive/build/libdoly_servo_control.a
    /home/pi/dolydev/libs/servo_drive/build/libdoly_drive.a
    /home/pi/dolydev/libs/Doly/libs/libServoMotor.a
    /home/pi/dolydev/libs/Doly/libs/libGpio.a
    /home/pi/dolydev/libs/Doly/libs/libTimer.a
    pthread
    spdlog
    fmt
    gpiod
    yaml-cpp
)
target_link_options(example PRIVATE -Wl,--end-group)
