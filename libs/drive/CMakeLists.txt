cmake_minimum_required(VERSION 3.16)
project(doly_drive VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -O2 -fPIC")
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# 依赖库
find_package(Threads REQUIRED)
find_package(PkgConfig REQUIRED)
find_package(yaml-cpp REQUIRED)
find_package(Python3 COMPONENTS Interpreter Development REQUIRED)

# ZeroMQ / GPIO
pkg_check_modules(ZMQ REQUIRED libzmq)
pkg_check_modules(GPIOD REQUIRED libgpiod>=1.6)

# 包含目录
include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../Doly/include  # 官方 Doly API
    ${GPIOD_INCLUDE_DIRS}
    ${ZMQ_INCLUDE_DIRS}
)

# 源文件
set(EXTIO_SOURCES
    src/pca9535_hal.cpp
    src/pca9535_patterns.cpp
    src/pca9535_service.cpp
    src/pca9535_config_v2.cpp
    src/pca9535_bus_adapter.cpp
    src/pca9535_ovos_bridge.cpp
    src/zmq_control_receiver.cpp  # Phase 4: ZeroMQ 控制命令接收器
    src/zmq_publisher.cpp          # ZeroMQ 发布器
    # Phase 2: 电机控制模块（自研）
    src/motor_controller.cpp       # 电机控制（已移除 PCA9685 初始化）
    src/  # 动画系统专用 API
    src/pid_controller.cpp         # PID 控制器
    src/encoder_reader.cpp         # 编码器读取
    src/safety_monitor.cpp         # 安全监控
    # Phase 2: 舵机控制模块（官方 API 封装）
    # Phase 3: SharedState 工具（EyeEngine 状态发布）
    src/shared_state_utils.cpp     # EyeState 原子读写操作
    src/servo_controller.cpp       # 舵机控制器（薄包装 ServoMotor API）
    # Phase 2: LED 控制模块（官方 API 封装）
    src/led_controller.cpp         # LED 控制器（薄包装 Gpio PWM API）
    # 重构：模块化架构
    src/sensor_subscriber.cpp      # 传感器订阅（TOF、IMU）
    src/config_loader.cpp          # 配置加载器
    src/lifecycle_manager.cpp      # 生命周期管理
    src/drive_service.cpp          # Drive 服务主类
)

# 官方 Doly 库路径
set(DOLY_LIB_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../Doly/libs")
set(DOLY_SDK_LIB_DIR "/home/pi/DOLY-DIY/SDK/lib")
set(DOLY_SERVO_LIB "${DOLY_LIB_DIR}/libServoMotor.a")
set(DOLY_GPIO_LIB "${DOLY_LIB_DIR}/libGpio.a")
set(DOLY_TIMER_LIB "${DOLY_LIB_DIR}/libTimer.a")
set(DOLY_DRIVE_CONTROL_LIB "${DOLY_LIB_DIR}/libDriveControl.so")
set(DOLY_RUNTIME_PATHS
    "${DOLY_SDK_LIB_DIR}"
    "${DOLY_LIB_DIR}"
    "/usr/local/lib"
)

# 静态库
add_library(doly_drive STATIC ${EXTIO_SOURCES})
target_link_libraries(doly_drive PUBLIC
    spdlog
    fmt
    Threads::Threads
    ${GPIOD_LIBRARIES}
    ${ZMQ_LIBRARIES}
    yaml-cpp
    # 官方 Doly 库
    ${DOLY_SERVO_LIB}
    ${DOLY_GPIO_LIB}
    ${DOLY_TIMER_LIB}
    ${DOLY_DRIVE_CONTROL_LIB}
)
set_target_properties(doly_drive PROPERTIES
    BUILD_RPATH "${DOLY_RUNTIME_PATHS}"
    INSTALL_RPATH "${DOLY_RUNTIME_PATHS}"
)

# 链接 doly_bus（如果已构建）
if(TARGET doly_bus)
    target_link_libraries(doly_drive doly_bus)
elseif(DOLY_BUS_LIB)
    target_link_libraries(doly_drive ${DOLY_BUS_LIB} Python3::Python)
endif()

# 安装
install(TARGETS doly_drive DESTINATION lib)
install(DIRECTORY include/doly DESTINATION include)

# 测试（可选）
# 使用方法：
#   cmake .. -DBUILD_TESTS=ON   # 编译测试文件
#   cmake .. -DBUILD_TESTS=OFF  # 不编译测试文件（加快编译速度）
option(BUILD_TESTS "Build tests" OFF)


# ============================================================
# ExtIO Service - 独立硬件服务可执行文件
# ============================================================
add_executable(drive_service src/drive_service_main.cpp)

# 链接 doly_bus（必须在前）
if(TARGET doly_bus)
    target_link_libraries(drive_service 
        doly_drive
        ${DOLY_DRIVE_CONTROL_LIB}
        doly_bus
        Threads::Threads
        ${GPIOD_LIBRARIES}
        ${ZMQ_LIBRARIES}
        yaml-cpp
        Python3::Python
        rt  # 共享内存支持
    )
elseif(DOLY_BUS_LIB)
    target_link_libraries(drive_service 
        doly_drive
        ${DOLY_DRIVE_CONTROL_LIB}
        ${DOLY_BUS_LIB}
        Threads::Threads
        ${GPIOD_LIBRARIES}
        ${ZMQ_LIBRARIES}
        yaml-cpp
        Python3::Python
        rt  # 共享内存支持
    )
else()
    target_link_libraries(drive_service 
        doly_drive
        ${DOLY_DRIVE_CONTROL_LIB}
        Threads::Threads
        ${GPIOD_LIBRARIES}
        ${ZMQ_LIBRARIES}
        yaml-cpp
        rt  # 共享内存支持
    )
endif()
set_target_properties(drive_service PROPERTIES
    BUILD_RPATH "${DOLY_RUNTIME_PATHS}"
    INSTALL_RPATH "${DOLY_RUNTIME_PATHS}"
)

# 安装可执行文件
install(TARGETS drive_service DESTINATION bin)
