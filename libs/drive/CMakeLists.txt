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

# ZeroMQ (使用 pkg-config)
pkg_check_modules(ZMQ REQUIRED libzmq)

# libgpiod - 用于读取 gpiochip2（PCA9535 内核导出的 GPIO）
cmake_minimum_required(VERSION 3.16)
project(doly_drive VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -O2")

# 依赖库
find_package(Threads REQUIRED)
find_package(PkgConfig REQUIRED)
find_package(yaml-cpp REQUIRED)
find_package(Python3 COMPONENTS Interpreter Development REQUIRED)

# ZeroMQ (使用 pkg-config)
pkg_check_modules(ZMQ REQUIRED libzmq)

# libgpiod (PCA9535 GPIO 访问)
pkg_check_modules(GPIOD REQUIRED libgpiod>=1.6)

# 包含目录
include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../Doly/include  # 官方 Doly API
    ${GPIOD_INCLUDE_DIRS}
    ${ZMQ_INCLUDE_DIRS}
)

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
    src/motor_controller_animation_api.cpp  # 动画系统专用 API
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
set(DOLY_SERVO_LIB "${DOLY_LIB_DIR}/libServoMotor.a")
set(DOLY_GPIO_LIB "${DOLY_LIB_DIR}/libGpio.a")
set(DOLY_TIMER_LIB "${DOLY_LIB_DIR}/libTimer.a")

# 静态库
add_library(doly_drive STATIC ${EXTIO_SOURCES})
target_link_libraries(doly_drive 
    Threads::Threads
    ${GPIOD_LIBRARIES}
    ${ZMQ_LIBRARIES}
    yaml-cpp
    # 官方 Doly 库
    ${DOLY_SERVO_LIB}
    ${DOLY_GPIO_LIB}
    ${DOLY_TIMER_LIB}
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

if(BUILD_TESTS)
    enable_testing()
    message(STATUS "[ExtIO] Building test executables...")
    
    # PCA9535 完整功能测试
    add_executable(test_pca9535 tests/test_pca9535.cpp)
    target_link_libraries(test_pca9535 
        doly_drive 
        Threads::Threads
        ${GPIOD_LIBRARIES}
    )
    add_test(NAME pca9535_test COMMAND test_pca9535)

    # 硬件使能控制测试
    add_executable(test_pca9535_enable tests/test_pca9535_enable.cpp)
    target_link_libraries(test_pca9535_enable 
        doly_drive 
        Threads::Threads
        ${GPIOD_LIBRARIES}
    )
    add_test(NAME pca9535_enable_test COMMAND test_pca9535_enable)

    # 完整功能测试（YAML + 历史数据）
    add_executable(test_pca9535_full tests/test_pca9535_full.cpp)
    target_link_libraries(test_pca9535_full 
        doly_drive 
        Threads::Threads
        ${GPIOD_LIBRARIES}
        yaml-cpp
    )
    add_test(NAME pca9535_full_test COMMAND test_pca9535_full)
    
    # use conditional block for tests that require doly_bus
    if(TARGET doly_bus OR DOLY_BUS_LIB)
        # sub-test uses doly_bus for message subscription
        target_link_options(test_pca9535_sub PRIVATE -Wl,--no-as-needed)
        if(TARGET doly_bus)
            target_link_libraries(test_pca9535_sub doly_bus zmq)
        else()
            target_link_libraries(test_pca9535_sub ${DOLY_BUS_LIB} zmq)
        endif()
        add_test(NAME pca9535_sub_test COMMAND test_pca9535_sub)

        # 消息总线集成测试（完整链接 ZMQ）
        add_executable(test_pca9535_bus tests/test_pca9535_bus.cpp)
        target_include_directories(test_pca9535_bus PRIVATE ${ZMQ_INCLUDE_DIRS})
        # note: doly_bus is static and depends on ZMQ, link with --no-as-needed group
        target_link_libraries(test_pca9535_bus 
            doly_drive 
            Threads::Threads
            ${GPIOD_LIBRARIES}
            yaml-cpp
        )
        target_link_options(test_pca9535_bus PRIVATE -Wl,--no-as-needed)
        if(TARGET doly_bus)
            target_link_libraries(test_pca9535_bus doly_bus zmq)
        else()
            target_link_libraries(test_pca9535_bus ${DOLY_BUS_LIB} zmq)
        endif()
        add_test(NAME pca9535_bus_test COMMAND test_pca9535_bus)

        # IO 状态同步测试
        add_executable(test_io_state_sync tests/test_io_state_sync.cpp)
        target_include_directories(test_io_state_sync PRIVATE ${ZMQ_INCLUDE_DIRS})
        target_link_libraries(test_io_state_sync 
            doly_drive 
            Threads::Threads
            ${GPIOD_LIBRARIES}
            yaml-cpp
        )
        target_link_options(test_io_state_sync PRIVATE -Wl,--no-as-needed)
        if(TARGET doly_bus)
            target_link_libraries(test_io_state_sync doly_bus zmq)
        else()
            target_link_libraries(test_io_state_sync ${DOLY_BUS_LIB} zmq)
        endif()
        add_test(NAME io_state_sync_test COMMAND test_io_state_sync)

    else()
        message(WARNING "[ExtIO] doly_bus not found; skipping pca9535_sub, pca9535_bus and io_state_sync tests")
    endif()

    # OVOS bridge test
    add_executable(test_pca9535_ovos_bridge tests/test_pca9535_ovos_bridge.cpp)
    if(TARGET doly_bus)
        target_link_libraries(test_pca9535_ovos_bridge 
            doly_drive 
            doly_bus
            Threads::Threads
            ${GPIOD_LIBRARIES}
            ${ZMQ_LIBRARIES}
            yaml-cpp
        )
    elseif(DOLY_BUS_LIB)
        target_link_libraries(test_pca9535_ovos_bridge 
            doly_drive 
            ${DOLY_BUS_LIB}
            Python3::Python
            Threads::Threads
            ${GPIOD_LIBRARIES}
            ${ZMQ_LIBRARIES}
            yaml-cpp
        )
    else()
        target_link_libraries(test_pca9535_ovos_bridge 
            doly_drive 
            Threads::Threads
            ${GPIOD_LIBRARIES}
            ${ZMQ_LIBRARIES}
            yaml-cpp
        )
    endif()
    add_test(NAME pca9535_ovos_bridge_test COMMAND test_pca9535_ovos_bridge)
else()
    message(STATUS "[ExtIO] Skipping test executables (use -DBUILD_TESTS=ON to build)")
endif()

# Add the test_motor_with_encoder executable (对标 test_motor_with_encoder.py)
add_executable(test_motor_with_encoder tests/test_motor_with_encoder.cpp)
target_include_directories(test_motor_with_encoder PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_options(test_motor_with_encoder PRIVATE -Wall -Wextra -O3 -pthread)
# link the core implementation (Doly GPIO API 由 doly_drive 提供)
target_link_libraries(test_motor_with_encoder PRIVATE doly_drive Threads::Threads)

# Add EyeState unit test
add_executable(test_eye_state tests/test_eye_state.cpp src/shared_state_utils.cpp)
target_include_directories(test_eye_state PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_options(test_eye_state PRIVATE -Wall -Wextra -O2 -pthread)
target_link_libraries(test_eye_state PRIVATE Threads::Threads)
add_test(NAME eye_state_test COMMAND test_eye_state)

# ============================================================
# ExtIO Service - 独立硬件服务可执行文件
# ============================================================
add_executable(drive_service src/drive_service_main.cpp)

# 链接 doly_bus（必须在前）
if(TARGET doly_bus)
    target_link_libraries(drive_service 
        doly_drive
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
        Threads::Threads
        ${GPIOD_LIBRARIES}
        ${ZMQ_LIBRARIES}
        yaml-cpp
        rt  # 共享内存支持
    )
endif()

# 安装可执行文件
install(TARGETS drive_service DESTINATION bin)
