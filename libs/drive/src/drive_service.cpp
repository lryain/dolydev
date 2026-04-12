#include <sdk/DriveControl.h>
/**
 * @file drive_service.cpp
 * @brief Drive 硬件服务实现
 

## 许可
GNU General Public License v3.0

## 作者
Kevin.Liu @ Make&Share
QQ: 47129927@qq.com

*/

#include "drive/drive_service.hpp"
#include "drive/lifecycle_manager.hpp"
#include "drive/motor_controller.hpp"
#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>
#include <iostream>
#include <iomanip>
#include <sstream>
#include <nlohmann/json.hpp>
#include <chrono>
#include <fstream>
#include <array>
#include <cmath>
#include <cctype>
#include <ctime>
#include <unordered_map>
#include <algorithm>
#include <filesystem>
#include <thread>
#include <linux/i2c.h>
#include <linux/i2c-dev.h>
#include <sys/ioctl.h>
#include <cerrno>
#include <cstring>

using json = nlohmann::json;

#define LOG_PREFIX "[ExtIOService]"

namespace {
constexpr char kDriveZmqEndpoint[] = "ipc:///tmp/doly_zmq.sock";

constexpr int kDefaultTofBus = 6;
constexpr uint8_t kDefaultTofLeftAddr = 0x29;
constexpr uint8_t kDefaultTofRightAddr = 0x2A;
constexpr char kDefaultTofMarker[] = "/tmp/tof_demo_setup_done";

std::string format_errno(const std::string& prefix) {
    return prefix + ": " + std::string(strerror(errno));
}

bool i2c_probe_device(int bus, uint8_t addr, std::string& err) {
    std::string dev = "/dev/i2c-" + std::to_string(bus);
    int fd = ::open(dev.c_str(), O_RDWR);
    if (fd < 0) {
        err = format_errno("open " + dev);
        return false;
    }

    if (ioctl(fd, I2C_SLAVE, addr) < 0) {
        err = format_errno("ioctl I2C_SLAVE addr=" + std::to_string(addr));
        ::close(fd);
        return false;
    }

    uint8_t buf = 0;
    struct i2c_rdwr_ioctl_data packets;
    struct i2c_msg messages[1];
    messages[0].addr = addr;
    messages[0].flags = I2C_M_RD;
    messages[0].len = 1;
    messages[0].buf = &buf;
    packets.msgs = messages;
    packets.nmsgs = 1;

    bool ok = (ioctl(fd, I2C_RDWR, &packets) >= 0);
    if (!ok) {
        err = format_errno("probe addr=" + std::to_string(addr));
    }
    ::close(fd);
    return ok;
}

bool i2c_write_reg_u8(int bus, uint8_t addr, uint16_t reg, uint8_t value, std::string& err) {
    std::string dev = "/dev/i2c-" + std::to_string(bus);
    int fd = ::open(dev.c_str(), O_RDWR);
    if (fd < 0) {
        err = format_errno("open " + dev);
        return false;
    }

    uint8_t outbuf[3] = {static_cast<uint8_t>(reg >> 8), static_cast<uint8_t>(reg & 0xFF), value};
    struct i2c_rdwr_ioctl_data packets;
    struct i2c_msg messages[1];
    messages[0].addr = addr;
    messages[0].flags = 0;
    messages[0].len = sizeof(outbuf);
    messages[0].buf = outbuf;
    packets.msgs = messages;
    packets.nmsgs = 1;

    if (ioctl(fd, I2C_RDWR, &packets) < 0) {
        err = format_errno("i2c write reg" + std::to_string(reg));
        ::close(fd);
        return false;
    }

    ::close(fd);
    return true;
}
}

// 从配置文件解析默认 LED 效果、颜色、RGB 序列、亮度和补偿
// 支持的 key:
//  - default_effect: solid|breath|blink|off|fade
//  - default_color: "R,G,B" 或 颜色名 (red,green,blue,yellow,orange,white,cyan,magenta,black)
//  - breath_color: 用于 breath 效果的颜色名或 RGB
//  - rgb_sequence: RGB 顺序矫正 (e.g., "RGB", "BRG")
//  - default_brightness: 亮度 (0-255)
//  - r_offset, g_offset, b_offset: RGB 通道补偿 (-100 到 +100)
static std::tuple<doly::drive::LedEffect, std::array<uint8_t,3>, std::string, uint8_t, int8_t, int8_t, int8_t> ReadDefaultLedConfig(const std::string& path = "config/rgb_default.cfg") {
    using LedEffect = doly::drive::LedEffect;
    std::array<uint8_t,3> fallback_color = {0, 50, 0};
    LedEffect fallback_effect = LedEffect::LED_SOLID;
    std::string fallback_sequence = "RGB";
    uint8_t fallback_brightness = 255;
    int8_t fallback_r_offset = 0, fallback_g_offset = 0, fallback_b_offset = 0;

    std::ifstream ifs(path);
    if (!ifs.is_open()) {
        // 无配置文件，返回默认值
        return {fallback_effect, fallback_color, fallback_sequence, fallback_brightness, fallback_r_offset, fallback_g_offset, fallback_b_offset};
    }

    std::unordered_map<std::string, std::string> kv;
    std::string line;
    while (std::getline(ifs, line)) {
        // 去除注释 (# 或 ;)
        auto pos = line.find_first_of("#;");
        if (pos != std::string::npos) line = line.substr(0, pos);
        // 去除两端空白
        auto ltrim = [](std::string &s){ s.erase(s.begin(), std::find_if(s.begin(), s.end(), [](int ch){ return !std::isspace(ch); })); };
        auto rtrim = [](std::string &s){ s.erase(std::find_if(s.rbegin(), s.rend(), [](int ch){ return !std::isspace(ch); }).base(), s.end()); };
        ltrim(line); rtrim(line);
        if (line.empty()) continue;
        auto eq = line.find(':');
        if (eq == std::string::npos) eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string key = line.substr(0, eq);
        std::string val = line.substr(eq + 1);
        ltrim(key); rtrim(key); ltrim(val); rtrim(val);
        // 小写化 key 与 val
        std::transform(key.begin(), key.end(), key.begin(), [](unsigned char c){ return std::tolower(c); });
        std::transform(val.begin(), val.end(), val.begin(), [](unsigned char c){ return std::tolower(c); });
        kv[key] = val;
    }

    // effect
    LedEffect effect = fallback_effect;
    if (kv.count("default_effect")) {
        std::string e = kv["default_effect"];
        if (e == "breath" || e == "breathe") effect = LedEffect::LED_BREATH;
        else if (e == "blink") effect = LedEffect::LED_BLINK;
        else if (e == "off") effect = LedEffect::LED_OFF;
        else if (e == "fade") effect = LedEffect::LED_FADE;
        else effect = LedEffect::LED_SOLID;
    }

    // color parsing helper
    auto parse_color = [&](const std::string &s)->std::array<uint8_t,3> {
        std::array<uint8_t,3> ret = {0,0,0};
        if (s.find(',') != std::string::npos) {
            std::istringstream ss(s);
            int r=0,g=0,b=0; char comma;
            if ((ss >> r >> comma >> g >> comma >> b)) {
                ret[0] = static_cast<uint8_t>(std::clamp(r, 0, 255));
                ret[1] = static_cast<uint8_t>(std::clamp(g, 0, 255));
                ret[2] = static_cast<uint8_t>(std::clamp(b, 0, 255));
                return ret;
            }
        }
        static const std::unordered_map<std::string, std::array<uint8_t,3>> cmap = {
            {"red", {255,0,0}}, {"green", {0,255,0}}, {"blue", {0,0,255}},
            {"yellow", {255,200,0}}, {"orange", {255,140,0}}, {"white", {255,255,255}},
            {"cyan", {0,255,255}}, {"magenta", {255,0,255}}, {"black", {0,0,0}},
            {"default", {0,50,0}}
        };
        auto it = cmap.find(s);
        if (it != cmap.end()) return it->second;
        // 如果没有命中，尝试将单个数字当做灰度
        try {
            int v = std::stoi(s);
            v = std::clamp(v, 0, 255);
            return {static_cast<uint8_t>(v), static_cast<uint8_t>(v), static_cast<uint8_t>(v)};
        } catch(...) {}
        return ret;
    };

    // priority: default_color -> (if breath effect) breath_color -> fallback
    std::array<uint8_t,3> color = fallback_color;
    if (kv.count("default_color")) {
        color = parse_color(kv["default_color"]);
    } else if (effect == LedEffect::LED_BREATH && kv.count("breath_color")) {
        color = parse_color(kv["breath_color"]);
    }

    // sequence
    std::string sequence = fallback_sequence;
    if (kv.count("rgb_sequence")) {
        sequence = kv["rgb_sequence"];
    }

    // brightness
    uint8_t brightness = fallback_brightness;
    if (kv.count("default_brightness")) {
        try {
            int b = std::stoi(kv["default_brightness"]);
            brightness = static_cast<uint8_t>(std::clamp(b, 0, 255));
        } catch(...) {
            brightness = fallback_brightness;
        }
    }

    // RGB offsets
    int8_t r_offset = fallback_r_offset, g_offset = fallback_g_offset, b_offset = fallback_b_offset;
    if (kv.count("r_offset")) {
        try {
            int o = std::stoi(kv["r_offset"]);
            r_offset = static_cast<int8_t>(std::clamp(o, -100, 100));
        } catch(...) {}
    }
    if (kv.count("g_offset")) {
        try {
            int o = std::stoi(kv["g_offset"]);
            g_offset = static_cast<int8_t>(std::clamp(o, -100, 100));
        } catch(...) {}
    }
    if (kv.count("b_offset")) {
        try {
            int o = std::stoi(kv["b_offset"]);
            b_offset = static_cast<int8_t>(std::clamp(o, -100, 100));
        } catch(...) {}
    }

    return {effect, color, sequence, brightness, r_offset, g_offset, b_offset};
}

namespace doly::drive {

DriveService::DriveService()
    : initialized_(false), shared_state_(nullptr), shm_fd_(-1) {
}

DriveService::~DriveService() {
    shutdown();
}

bool DriveService::initialize(const std::string& config_file) {
    std::cout << LOG_PREFIX << " ======================================" << std::endl;
    std::cout << LOG_PREFIX << " Initializing Drive Service..." << std::endl;
    std::cout << LOG_PREFIX << " Version: 3.0" << std::endl;
    std::cout << LOG_PREFIX << " ======================================" << std::endl;
    
    try {
        // 加载配置
        auto load_result = ConfigLoader::load(config_file);
        if (!load_result.success) {
            std::cout << LOG_PREFIX << " WARNING: Using default configuration" << std::endl;
        }
        
        // 初始化各子系统
        if (!init_shared_memory()) {
            std::cerr << LOG_PREFIX << " ERROR: Failed to initialize shared memory" << std::endl;
            return false;
        }
        
        if (!init_zmq_bus()) {
            std::cerr << LOG_PREFIX << " ERROR: Failed to initialize ZeroMQ bus" << std::endl;
            return false;
        }
        
        if (!init_hardware_service(load_result.config)) {
            std::cerr << LOG_PREFIX << " WARNING: Failed to initialize hardware service, continuing in mock mode" << std::endl;
            // continue without hardware controllers for debugging/mocks
            // controllers may be null; execute_* will guard against null pointers
        }

        // 根据配置自动配置 TOF 传感器地址
        if (load_result.config.auto_configure_tof && hw_service_) {
            std::cout << "[PCA9535 Service] auto_configure_tof enabled" << std::endl;
            json tof_cmd;
            tof_cmd["action"] = "set_tof_address";
            tof_cmd["force"] = false;  // 如果已配置则跳过
            tof_cmd["bus"] = 6;
            tof_cmd["left_addr"] = 0x29;
            tof_cmd["right_addr"] = 0x30;
            tof_cmd["from_addr"] = 0x29;
            bool tof_ok = handle_set_tof_address(tof_cmd);
            (void)tof_ok;
        }
        
        if (!init_controllers(load_result.config)) {
            std::cerr << LOG_PREFIX << " WARNING: Failed to initialize controllers, continuing in mock mode" << std::endl;
            // continue even if controllers failed; execute_* will check for controller presence
        }
        
        if (!init_control_receiver()) {
            std::cerr << LOG_PREFIX << " ERROR: Failed to initialize control receiver" << std::endl;
            return false;
        }
        
        if (!start_sensor_subscriber(load_result.sensor_log_config)) {
            std::cerr << LOG_PREFIX << " ERROR: Failed to start sensor subscriber" << std::endl;
            return false;
        }
        
        // 启动命令队列执行线程
        start_command_executors();
        
        initialized_ = true;
        
        std::cout << LOG_PREFIX << " ======================================" << std::endl;
        std::cout << LOG_PREFIX << " Drive Service READY!" << std::endl;
        std::cout << LOG_PREFIX << " ======================================" << std::endl;
        
        return true;
        
    } catch (const std::exception& e) {
        std::cerr << LOG_PREFIX << " FATAL: " << e.what() << std::endl;
        return false;
    }
}

bool DriveService::init_shared_memory() {
    std::cout << LOG_PREFIX << " [1/5] Initializing shared memory..." << std::endl;
    
    shm_fd_ = shm_open(SHARED_STATE_NAME, O_CREAT | O_RDWR, 0666);
    if (shm_fd_ < 0) {
        std::cerr << LOG_PREFIX << " ERROR: Cannot create shared memory" << std::endl;
        return false;
    }
    
    if (ftruncate(shm_fd_, SHARED_STATE_SIZE) != 0) {
        std::cerr << LOG_PREFIX << " ERROR: Cannot set shared memory size" << std::endl;
        close(shm_fd_);
        shm_fd_ = -1;
        return false;
    }
    
    shared_state_ = static_cast<SharedState*>(
        mmap(nullptr, SHARED_STATE_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd_, 0));
    
    if (shared_state_ == MAP_FAILED) {
        std::cerr << LOG_PREFIX << " ERROR: Cannot map shared memory" << std::endl;
        close(shm_fd_);
        shm_fd_ = -1;
        return false;
    }
    
    new (shared_state_) SharedState();
    
    std::cout << LOG_PREFIX << " ✅ Shared memory OK: 0x" << std::hex << shared_state_->magic 
              << ", Version: " << std::dec << shared_state_->version << std::endl;
    
    return true;
}

bool DriveService::init_zmq_bus() {
    std::cout << LOG_PREFIX << " [2/5] Initializing ZeroMQ bus..." << std::endl;
    event_publisher_ = std::make_shared<doly::ZmqPublisher>(kDriveZmqEndpoint, "drive_service_status", true);
    if (!event_publisher_ || !event_publisher_->is_ready()) {
        std::cerr << LOG_PREFIX << " ERROR: ZmqPublisher failed to initialize" << std::endl;
        return false;
    }

    std::cout << LOG_PREFIX << " ✅ ZeroMQ bus started" << std::endl;
    return true;
}

bool DriveService::init_hardware_service(const doly::extio::Pca9535ConfigV2& config) {
    std::cout << LOG_PREFIX << " [3/5] Initializing PCA9535 hardware service..." << std::endl;
    
    // 打印配置摘要
    std::cout << LOG_PREFIX << " Config Summary: servo_left=" << (config.enable_servo_left_default ? "ON" : "OFF")
              << ", servo_right=" << (config.enable_servo_right_default ? "ON" : "OFF")
              << ", tof=" << (config.enable_tof_default ? "ON" : "OFF")
              << ", cliff=" << (config.enable_cliff_default ? "ON" : "OFF") << std::endl;
    
    hw_service_ = std::make_shared<doly::extio::Pca9535Service>();
    
    if (shared_state_) {
        hw_service_->setSharedState(shared_state_);
    }
    
    hw_service_->apply_config_to_recognizers(config);
    
    if (!hw_service_->init(&config)) {
        std::cerr << LOG_PREFIX << " ERROR: Cannot initialize hardware service" << std::endl;
        return false;
    }
    
    bus_adapter_ = std::make_shared<doly::extio::Pca9535BusAdapter>(*hw_service_, config, event_publisher_);
    if (!bus_adapter_->start()) {
        std::cerr << LOG_PREFIX << " ERROR: Cannot start bus adapter" << std::endl;
        return false;
    }
    
    if (!hw_service_->start()) {
        std::cerr << LOG_PREFIX << " ERROR: Cannot start hardware service IRQ thread" << std::endl;
        return false;
    }
    
    std::cout << LOG_PREFIX << " ✅ Hardware service ready" << std::endl;
    return true;
}

bool DriveService::init_controllers(const doly::extio::Pca9535ConfigV2& config) {
    std::cout << LOG_PREFIX << " [4/5] Initializing controllers..." << std::endl;
    
    // Servo
    servo_ctrl_ = std::make_shared<doly::drive::ServoController>();
    
    // P1.3 优化：先设置电源控制回调，再进行 Init
    // 这样在 Init(initial_angles) 时能保证先写 PWM 后上电
    servo_ctrl_->SetPowerCallback([this](ServoChannel ch, bool enable) {
        if (hw_service_) {
            std::cout << LOG_PREFIX << " [ServoPower] Callback: " << (ch == SERVO_LEFT ? "Left" : "Right") 
                      << " -> " << (enable ? "ON" : "OFF") << std::endl;
            if (ch == SERVO_LEFT) {
                hw_service_->enable_servo_left(enable);
            } else if (ch == SERVO_RIGHT) {
                hw_service_->enable_servo_right(enable);
            }
        }
    });

    // 准备初始角度和自动保持配置
    std::map<ServoChannel, float> initial_angles;
    std::map<ServoChannel, bool> auto_hold_modes;
    
    if (config.enable_servo_left_default) {
        initial_angles[SERVO_LEFT] = (float)config.servo.left.default_angle;
    }
    if (config.enable_servo_right_default) {
        initial_angles[SERVO_RIGHT] = (float)config.servo.right.default_angle;
    }
    
    auto_hold_modes[SERVO_LEFT] = config.servo.left.enable_autohold;
    auto_hold_modes[SERVO_RIGHT] = config.servo.right.enable_autohold;

    // 执行 Init
    if (!servo_ctrl_->Init(initial_angles, auto_hold_modes)) {
        std::cerr << LOG_PREFIX << " ERROR: Cannot initialize servo controller" << std::endl;
        return false;
    }
    
    // 应用其它配置
    servo_ctrl_->SetAutoHold(SERVO_LEFT, 
                             config.servo.left.enable_autohold,
                             config.servo.left.autohold_duration_ms);
    servo_ctrl_->SetAutoHold(SERVO_RIGHT,
                             config.servo.right.enable_autohold,
                             config.servo.right.autohold_duration_ms);

    std::cout << LOG_PREFIX << " Servos initialized at L=" 
              << config.servo.left.default_angle << " R=" << config.servo.right.default_angle << std::endl;
    
    // LED
    // 从配置文件读取默认 LED 效果、颜色、RGB 序列、亮度和补偿
    auto def = ReadDefaultLedConfig("/home/pi/dolydev/config/rgb_default.cfg");
    led_ctrl_ = std::make_shared<doly::drive::LedController>(std::get<2>(def));
    if (!led_ctrl_->Init()) {
        std::cerr << LOG_PREFIX << " ERROR: Cannot initialize LED controller" << std::endl;
        return false;
    }
    // 设置亮度和RGB补偿
    led_ctrl_->SetBrightness(std::get<3>(def));
    led_ctrl_->SetRgbOffsets(std::get<4>(def), std::get<5>(def), std::get<6>(def));
    // 保存默认状态（已注释：SetDefaultState 不存在于 LedController）
    // led_ctrl_->SetDefaultState(std::get<0>(def), std::get<1>(def));

    // 应用默认效果
    try {
        led_ctrl_->StartEffect(std::get<0>(def), std::get<1>(def));
        std::cout << LOG_PREFIX << " ✓ Applied default LED effect from config: (r=" << (int)std::get<1>(def)[0]
                  << ", g=" << (int)std::get<1>(def)[1] << ", b=" << (int)std::get<1>(def)[2] 
                  << ", sequence=" << std::get<2>(def) << ", brightness=" << (int)std::get<3>(def)
                  << ", offsets=" << (int)std::get<4>(def) << "," << (int)std::get<5>(def) << "," << (int)std::get<6>(def) << ")" << std::endl;
    } catch (...) {
        std::cerr << LOG_PREFIX << " WARN: Failed to start default LED effect" << std::endl;
    }

    // Motor
    motor_ctrl_ = std::make_shared<MotorController>("/dev/i2c-3", 0x40);
    if (!motor_ctrl_->init()) {
        std::cerr << LOG_PREFIX << " ERROR: Cannot initialize motor controller" << std::endl;
        return false;
    }
    motor_ctrl_->enableEncoders(true);
    
    std::cout << LOG_PREFIX << " ✅ Controllers ready" << std::endl;
    return true;
}

bool DriveService::init_control_receiver() {
    std::cout << LOG_PREFIX << " [5/5] Initializing control receiver..." << std::endl;
    
    control_receiver_ = std::make_unique<doly::extio::ZmqControlReceiver>(
        "ipc:///tmp/doly_control.sock");
    
    // 使用非阻塞队列系统
    control_receiver_->SetControlCallback([this](const std::string& topic, const json& cmd) {
        std::string action = cmd.value("action", "");
        
        // 对于需要队列的命令(LED/Servo/Motor),非阻塞入队
        CommandType type = classify_command(cmd);
        if (type != CommandType::UNKNOWN) {
            enqueue_command(topic, cmd);
            return;
        }
        
        // 对于其他命令,直接同步执行
        try {
            if (action == "enable_tof") {
                bool value = cmd.value("value", true);
                bool ok = hw_service_->enable_tof(value);
                std::cout << LOG_PREFIX << " ✓ enable_tof(" << (value ? "ON" : "OFF") << ") => " << (ok ? "OK" : "FAIL") << std::endl;
            } else if (action == "set_tof_address") {
                bool ok = handle_set_tof_address(cmd);
                std::cout << LOG_PREFIX << " ✓ set_tof_address => " << (ok ? "OK" : "FAIL") << std::endl;
            } else if (action == "enable_cliff") {
                bool value = cmd.value("value", true);
                bool ok = hw_service_->enable_cliff(value);
                std::cout << LOG_PREFIX << " ✓ enable_cliff(" << (value ? "ON" : "OFF") << ") => " << (ok ? "OK" : "FAIL") << std::endl;
            } else if (action == "set_ext_io") {
                std::string pin_str = cmd.value("pin", "");
                bool value = cmd.value("value", false);
                if (pin_str.rfind("EXT_IO_", 0) == 0) {
                    int io_num = std::stoi(pin_str.substr(7));
                    bool ok = hw_service_->set_ext_io(io_num, value);
                    std::cout << LOG_PREFIX << " ✓ set_ext_io(" << pin_str << ", " << (value ? "ON" : "OFF") << ") => " << (ok ? "OK" : "FAIL") << std::endl;
                }
            } else if (action == "set_outputs_bulk") {
                uint16_t state = cmd.value("state", (uint16_t)0);
                uint16_t mask = cmd.value("mask", (uint16_t)0);
                bool ok = hw_service_->set_outputs_bulk(state, mask);
                std::cout << LOG_PREFIX << " ✓ set_outputs_bulk(state=0x" << std::hex << state 
                          << ", mask=0x" << mask << std::dec << ") => " << (ok ? "OK" : "FAIL") << std::endl;
            }
        } catch (const std::exception& e) {
            std::cerr << LOG_PREFIX << " ERROR: " << e.what() << std::endl;
        }
    });
    
    if (!control_receiver_->Start()) {
        std::cerr << LOG_PREFIX << " ERROR: Cannot start control receiver" << std::endl;
        return false;
    }
    
    std::cout << LOG_PREFIX << " ✅ Control receiver ready (non-blocking queue mode)" << std::endl;
    
    // 启动 OVOS bridge
    ovos_bridge_ = std::make_unique<doly::extio::Pca9535OvosBridge>(*hw_service_);
    if (!ovos_bridge_->start()) {
        std::cerr << LOG_PREFIX << " ERROR: Cannot start OVOS bridge" << std::endl;
        return false;
    }
    
    return true;
}

bool DriveService::start_sensor_subscriber(const SensorLogConfig& config) {
    std::cout << LOG_PREFIX << " [5.5/5] Starting sensor subscriber..." << std::endl;
    
    sensor_subscriber_ = std::make_unique<SensorSubscriber>(shared_state_, config);
    if (!sensor_subscriber_->start()) {
        std::cerr << LOG_PREFIX << " ERROR: Cannot start sensor subscriber" << std::endl;
        return false;
    }
    
    return true;
}

void DriveService::shutdown() {
    std::cout << LOG_PREFIX << " Shutting down Drive Service..." << std::endl;
    
    // 停止命令队列执行线程
    stop_command_executors();
    
    if (sensor_subscriber_) {
        sensor_subscriber_->stop();
        sensor_subscriber_.reset();
    }
    
    if (control_receiver_) {
        control_receiver_->Stop();
        control_receiver_.reset();
    }
    
    if (ovos_bridge_) {
        ovos_bridge_->stop();
        ovos_bridge_.reset();
    }
    
    if (led_ctrl_) {
        led_ctrl_->TurnOff();
    }
    if (motor_ctrl_) {
        motor_ctrl_->stop();
    }
    
    motor_ctrl_.reset();
    led_ctrl_.reset();
    servo_ctrl_.reset();
    
    if (hw_service_) {
        hw_service_->stop();
        hw_service_.reset();
    }
    
    if (bus_adapter_) {
        bus_adapter_->stop();
        bus_adapter_.reset();
    }
    
    event_publisher_.reset();
    
    if (shared_state_) {
        munmap(shared_state_, SHARED_STATE_SIZE);
        shared_state_ = nullptr;
    }
    
    if (shm_fd_ >= 0) {
        close(shm_fd_);
        shm_unlink(SHARED_STATE_NAME);
        shm_fd_ = -1;
    }
    
    std::cout << LOG_PREFIX << " Drive Service stopped" << std::endl;
}

void DriveService::run() {
    int status_counter = 0;
    
    while (LifecycleManager::instance().is_running()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        
        // Update LED loop for auto-recovery (P1.4)
        if (led_ctrl_) {
            led_ctrl_->Loop();
        }

        // 每秒发布一次驱动状态
        if (++status_counter % 10 == 0) {
            try {
                json servo_status = {
                    {"timestamp", std::chrono::system_clock::now().time_since_epoch().count()},
                    {"active", servo_ctrl_->IsActive()}
                };
                auto msg = servo_status.dump();
                publish_event("status.drive.servos", msg);

                auto color = led_ctrl_->GetCurrentColor();
                json led_status = {
                    {"r", color[0]}, {"g", color[1]}, {"b", color[2]}
                };
                msg = led_status.dump();
                publish_event("status.drive.leds", msg);
                
            } catch (...) {
                // ignore
            }
        }
    }
}

bool DriveService::publish_event(const std::string& topic, const std::string& payload) {
    if (!event_publisher_ || !event_publisher_->is_ready()) {
        return false;
    }
    if (!event_publisher_->publish(topic, payload.data(), payload.size())) {
        std::cerr << LOG_PREFIX << " ERROR: Failed to publish topic " << topic << std::endl;
        return false;
    }
    return true;
}

// ============================================================================
// 命令队列系统实现
// ============================================================================

CommandType DriveService::classify_command(const json& cmd) {
    std::string action = cmd.value("action", "");
    
    // LED 命令
    if (action.find("led") != std::string::npos) {
        return CommandType::LED;
    }
    
    // Servo 命令  
    if (action.find("servo") != std::string::npos || action == "enable_servo" ||
        action == "lift_dumbbell" || action == "dumbbell_dance" || 
        action == "wave_flag" || action == "beat_drum" || action == "paddle_row" ||
        action == "set_servo_multi" || action == "set_angle") {
        return CommandType::SERVO;
    }
    
    // Motor 命令 (包含 motor, move_distance_cm, turn_deg, get_encoder_values, PID命令, 动画API, SDK 透传)
    if (action.find("motor") != std::string::npos ||
        action == "move_distance_cm" ||
        action == "go_xy" ||
        action == "turn_deg" ||
        action == "move_distance_cm_pid" ||
        action == "turn_deg_pid" ||
        action == "drive_distance" ||
        action == "drive_rotate" ||
        action == "drive_rotate_left" ||
        action == "drive_rotate_right" ||
        action == "drive_distance_pid" ||
        action == "turn_deg_pid_advanced" ||
        action == "get_encoder_values" ||
        action.find("sdk_") == 0) {
        return CommandType::MOTOR;
    }
    
    // 其他直接执行的命令(enable_tof, enable_cliff, set_ext_io等)
    // 这些不需要队列,在ZMQ回调中直接处理
    return CommandType::UNKNOWN;
}

bool DriveService::handle_set_tof_address(const json& cmd) {
    using namespace std::chrono_literals;
    int bus = cmd.value("bus", kDefaultTofBus);
    uint8_t left_addr = static_cast<uint8_t>(cmd.value("left_addr", static_cast<int>(kDefaultTofLeftAddr)));
    uint8_t right_addr = static_cast<uint8_t>(cmd.value("right_addr", static_cast<int>(kDefaultTofRightAddr)));
    uint8_t from_addr = static_cast<uint8_t>(cmd.value("from_addr", static_cast<int>(kDefaultTofLeftAddr)));
    std::string marker = cmd.value("marker", std::string(kDefaultTofMarker));
    bool force = cmd.value("force", false);

    std::cout << LOG_PREFIX << " [TOF] set_tof_address request bus=" << bus
              << " from=0x" << std::hex << static_cast<int>(from_addr)
              << " -> right=0x" << static_cast<int>(right_addr)
              << " left=0x" << static_cast<int>(left_addr)
              << std::dec << " force=" << (force ? "true" : "false") << std::endl;

    namespace fs = std::filesystem;
    auto probe_pair = [&](const std::string& stage) {
        std::string err_left, err_right;
        bool left_ok = i2c_probe_device(bus, left_addr, err_left);
        bool right_ok = i2c_probe_device(bus, right_addr, err_right);
        std::cout << LOG_PREFIX << " [TOF] probe(" << stage << ") left=0x" << std::hex
                  << static_cast<int>(left_addr) << " => " << (left_ok ? "OK" : err_left)
                  << " | right=0x" << static_cast<int>(right_addr) << " => "
                  << (right_ok ? "OK" : err_right) << std::dec << std::endl;
        return left_ok && right_ok;
    };

    // 检查是否已配置过
    if (!force && fs::exists(marker)) {
        std::cout << LOG_PREFIX << " [TOF] marker exists, verifying current bus addresses..." << std::endl;
        if (probe_pair("check")) {
            std::cout << LOG_PREFIX << " [TOF] addresses already configured correctly, skip" << std::endl;
            return true;
        }
        std::cout << LOG_PREFIX << " [TOF] marker present but addresses missing, will reconfigure" << std::endl;
        std::error_code ec;
        fs::remove(marker, ec);
    }

    // 先检查目标地址是否已经在线（可能前次改址已成功但marker未写）
    std::cout << LOG_PREFIX << " [TOF] checking target addresses before reconfiguration..." << std::endl;
    if (probe_pair("pre-check")) {
        std::cout << LOG_PREFIX << " [TOF] target addresses already present, skip write and go to marker" << std::endl;
        // 直接写marker，跳过I2C写操作
        std::ofstream ofs(marker, std::ios::trunc);
        if (ofs.is_open()) {
            ofs << "done " << std::time(nullptr) << " bus=" << bus
                << " left=0x" << std::hex << static_cast<int>(left_addr)
                << " right=0x" << std::hex << static_cast<int>(right_addr) << std::dec << "\n";
            ofs.close();
        }
        std::cout << LOG_PREFIX << " [TOF] marker file created (addresses already present)" << std::endl;
        return true;
    }

    // 执行改址过程
    std::cout << LOG_PREFIX << " [TOF] disabling left sensor via enable_tof(false)" << std::endl;
    bool disable_ok = hw_service_->enable_tof(false);
    if (!disable_ok) {
        std::cerr << LOG_PREFIX << " [TOF] failed to disable TOF_ENL" << std::endl;
    }
    std::this_thread::sleep_for(50ms);

    std::string err;
    bool write_ok = i2c_write_reg_u8(bus, from_addr, 0x0212, right_addr, err);
    if (!write_ok) {
        std::cerr << LOG_PREFIX << " [TOF] I2C write failed: " << err << " (continuing anyway)" << std::endl;
        // 继续执行，可能地址已经改过了
    } else {
        std::cout << LOG_PREFIX << " [TOF] wrote reg0x0212 on addr 0x" << std::hex
                  << static_cast<int>(from_addr) << " => 0x" << static_cast<int>(right_addr)
                  << std::dec << std::endl;
    }
    std::this_thread::sleep_for(40ms);

    bool reenable_ok = hw_service_->enable_tof(true);
    std::cout << LOG_PREFIX << " [TOF] re-enable left sensor => " << (reenable_ok ? "OK" : "FAIL") << std::endl;
    std::this_thread::sleep_for(80ms);

    // 最终验证
    bool both_ok = probe_pair("post");
    if (both_ok) {
        std::ofstream ofs(marker, std::ios::trunc);
        if (ofs.is_open()) {
            ofs << "done " << std::time(nullptr) << " bus=" << bus
                << " left=0x" << std::hex << static_cast<int>(left_addr)
                << " right=0x" << static_cast<int>(right_addr) << std::dec << "\n";
            ofs.close();
        }
        std::cout << LOG_PREFIX << " [TOF] ✅ address configuration SUCCESS, marker file written" << std::endl;
        return true;
    }

    std::cerr << LOG_PREFIX << " [TOF] ❌ address configuration FAILED, addresses not detected on bus" << std::endl;
    return false;
}

void DriveService::enqueue_command(const std::string& topic, const json& cmd) {
    printf("enqueue_command - topic: %s cmd: %s\n", topic.c_str(), cmd.dump().c_str());
    CommandType type = classify_command(cmd);
    std::string action = cmd.value("action", "");
    // 时间戳用于与 Python 侧日志对齐
    auto now_ts = std::chrono::system_clock::now();
    auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(now_ts.time_since_epoch()).count();
    
    switch (type) {
        case CommandType::LED: {
            std::lock_guard<std::mutex> lock(led_queue_mutex_);
            if (led_command_queue_.size() >= MAX_QUEUE_SIZE) {
                led_command_queue_.pop();
                std::cerr << LOG_PREFIX << " ⚠ LED queue full, dropped oldest command" << std::endl;
            }
            led_command_queue_.push(cmd);
            std::cout << LOG_PREFIX << " [LED Queue] Enqueued (" << led_command_queue_.size() << "): "
                      << action << " ts=" << now_ms << std::endl;
            led_queue_cv_.notify_one();
            break;
        }
        case CommandType::SERVO: {
            std::lock_guard<std::mutex> lock(servo_queue_mutex_);
            if (servo_command_queue_.size() >= MAX_QUEUE_SIZE) {
                servo_command_queue_.pop();
                std::cerr << LOG_PREFIX << " ⚠ Servo queue full, dropped oldest command" << std::endl;
            }
            servo_command_queue_.push(cmd);
            std::cout << LOG_PREFIX << " [Servo Queue] Enqueued (" << servo_command_queue_.size() << "): "
                      << action << " ts=" << now_ms << std::endl;
            servo_queue_cv_.notify_one();
            break;
        }
        case CommandType::MOTOR: {
            std::lock_guard<std::mutex> lock(motor_queue_mutex_);
            if (motor_command_queue_.size() >= MAX_QUEUE_SIZE) {
                motor_command_queue_.pop();
                std::cerr << LOG_PREFIX << " ⚠ Motor queue full, dropped oldest command" << std::endl;
            }
            motor_command_queue_.push(cmd);
            std::cout << LOG_PREFIX << " [Motor Queue] Enqueued (" << motor_command_queue_.size() << "): "
                      << action << " ts=" << now_ms << std::endl;
            motor_queue_cv_.notify_one();
            break;
        }
        default:
            std::cerr << LOG_PREFIX << " ⚠ Unknown command type: " << cmd.dump() << std::endl;
    }
}

void DriveService::start_command_executors() {
    executors_running_ = true;
    
    led_executor_thread_ = std::thread(&DriveService::led_executor_loop, this);
    servo_executor_thread_ = std::thread(&DriveService::servo_executor_loop, this);
    motor_executor_thread_ = std::thread(&DriveService::motor_executor_loop, this);
    
    std::cout << LOG_PREFIX << " ✅ Command executor threads started" << std::endl;
}

void DriveService::stop_command_executors() {
    executors_running_ = false;
    
    // 通知所有线程退出
    led_queue_cv_.notify_all();
    servo_queue_cv_.notify_all();
    motor_queue_cv_.notify_all();
    
    // 等待线程结束
    if (led_executor_thread_.joinable()) {
        led_executor_thread_.join();
    }
    if (servo_executor_thread_.joinable()) {
        servo_executor_thread_.join();
    }
    if (motor_executor_thread_.joinable()) {
        motor_executor_thread_.join();
    }
    
    std::cout << LOG_PREFIX << " ✅ Command executor threads stopped" << std::endl;
}

void DriveService::led_executor_loop() {
    auto now_ts = std::chrono::system_clock::now();
    auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(now_ts.time_since_epoch()).count();
    std::cout << LOG_PREFIX << " [LED Executor] Thread started ts=" << now_ms << std::endl;
    
    while (executors_running_) {
        std::unique_lock<std::mutex> lock(led_queue_mutex_);
        
        // 等待命令或超时
        led_queue_cv_.wait_for(lock, std::chrono::milliseconds(100), 
            [this] { return !led_command_queue_.empty() || !executors_running_; });
        
        if (!executors_running_) {
            break;
        }
        
        if (!led_command_queue_.empty()) {
            json cmd = led_command_queue_.front();
            led_command_queue_.pop();
            size_t queue_size = led_command_queue_.size();
            lock.unlock();
            
            // 异步执行命令
            try {
                auto exec_ts = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
                std::cout << LOG_PREFIX << " [LED Executor] Start exec ts=" << exec_ts << " action=" << cmd.value("action", "") << std::endl;
                execute_led_command(cmd);
                auto done_ts = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
                std::cout << LOG_PREFIX << " [LED Executor] Done exec ts=" << done_ts << " action=" << cmd.value("action", "") << std::endl;
                if (queue_size > 5) {
                    std::cout << LOG_PREFIX << " [LED Executor] Queue depth: " << queue_size << std::endl;
                }
            } catch (const std::exception& e) {
                std::cerr << LOG_PREFIX << " [LED Executor] ERROR: " << e.what() << std::endl;
            }
        }
    }
    
    std::cout << LOG_PREFIX << " [LED Executor] Thread stopped" << std::endl;
}

void DriveService::servo_executor_loop() {
    auto now_ts = std::chrono::system_clock::now();
    auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(now_ts.time_since_epoch()).count();
    std::cout << LOG_PREFIX << " [Servo Executor] Thread started ts=" << now_ms << std::endl;
    
    while (executors_running_) {
        std::unique_lock<std::mutex> lock(servo_queue_mutex_);
        
        servo_queue_cv_.wait_for(lock, std::chrono::milliseconds(100), 
            [this] { return !servo_command_queue_.empty() || !executors_running_; });
        
        if (!executors_running_) {
            break;
        }
        
        if (!servo_command_queue_.empty()) {
            json cmd = servo_command_queue_.front();
            servo_command_queue_.pop();
            size_t queue_size = servo_command_queue_.size();
            lock.unlock();
            
            try {
                auto exec_ts = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
                std::cout << LOG_PREFIX << " [Servo Executor] Start exec ts=" << exec_ts << " action=" << cmd.value("action", "") << std::endl;
                execute_servo_command(cmd);
                auto done_ts = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
                std::cout << LOG_PREFIX << " [Servo Executor] Done exec ts=" << done_ts << " action=" << cmd.value("action", "") << std::endl;
                if (queue_size > 5) {
                    std::cout << LOG_PREFIX << " [Servo Executor] Queue depth: " << queue_size << std::endl;
                }
            } catch (const std::exception& e) {
                std::cerr << LOG_PREFIX << " [Servo Executor] ERROR: " << e.what() << std::endl;
            }
        }
    }
    
    std::cout << LOG_PREFIX << " [Servo Executor] Thread stopped" << std::endl;
}

void DriveService::motor_executor_loop() {
    auto now_ts = std::chrono::system_clock::now();
    auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(now_ts.time_since_epoch()).count();
    std::cout << LOG_PREFIX << " [Motor Executor] Thread started ts=" << now_ms << std::endl;
    
    while (executors_running_) {
        std::unique_lock<std::mutex> lock(motor_queue_mutex_);
        
        motor_queue_cv_.wait_for(lock, std::chrono::milliseconds(100), 
            [this] { return !motor_command_queue_.empty() || !executors_running_; });
        
        if (!executors_running_) {
            break;
        }
        
        if (!motor_command_queue_.empty()) {
            json cmd = motor_command_queue_.front();
            motor_command_queue_.pop();
            size_t queue_size = motor_command_queue_.size();
            lock.unlock();
            
            try {
                auto exec_ts = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
                std::cout << LOG_PREFIX << " [Motor Executor] Start exec ts=" << exec_ts << " action=" << cmd.value("action", "") << std::endl;
                execute_motor_command(cmd);
                auto done_ts = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
                std::cout << LOG_PREFIX << " [Motor Executor] Done exec ts=" << done_ts << " action=" << cmd.value("action", "") << std::endl;
                if (queue_size > 5) {
                    std::cout << LOG_PREFIX << " [Motor Executor] Queue depth: " << queue_size << std::endl;
                }
            } catch (const std::exception& e) {
                std::cerr << LOG_PREFIX << " [Motor Executor] ERROR: " << e.what() << std::endl;
            }
        }
    }
    
    std::cout << LOG_PREFIX << " [Motor Executor] Thread stopped" << std::endl;
}

void DriveService::execute_led_command(const json& cmd) {
    std::string action = cmd.value("action", "");
    
    if (action == "set_led_effect") {
        std::string effect_str = cmd.value("effect", "solid");
        uint8_t r = cmd.value("r", 255);
        uint8_t g = cmd.value("g", 255);
        uint8_t b = cmd.value("b", 255);
        std::string side = cmd.value("side", "");
        uint32_t duration_ms = cmd.value("duration_ms", 0);
        float frequency_hz = cmd.value("frequency_hz", cmd.value("frequency", 0.0f));
        uint32_t period_ms = cmd.value("period_ms", 0U);
        if (!(frequency_hz > 0.0f) && period_ms > 0) {
            frequency_hz = 1000.0f / static_cast<float>(period_ms);
        }
        
        LedEffect effect = LED_SOLID;
        if (effect_str == "breath") effect = LED_BREATH;
        else if (effect_str == "blink") effect = LED_BLINK;
        
        std::cout << LOG_PREFIX << " [LED Executor] ✓ Executing: effect=" << effect_str 
                  << " rgb=(" << (int)r << "," << (int)g << "," << (int)b << ") side=" << side 
                  << " duration=" << duration_ms << "ms"
                  << " frequency=" << std::fixed << std::setprecision(2) << frequency_hz << "Hz"
                  << std::defaultfloat << std::endl;
        
        if (led_ctrl_) {
            if (duration_ms > 0) {
                // 检查是否有恢复颜色参数
                uint32_t hold_duration = cmd.value("duration_ms", 0);
                if (hold_duration == 0) {
                    // 如果没有指定恢复持续时间，则使用 duration_ms 作为效果显示时间
                    led_ctrl_->StartEffect(effect, {r, g, b}, duration_ms, frequency_hz);
                } else {
                    // 如果有恢复参数，使用 SetEffectTemporary
                    uint8_t dr = cmd.value("default_r", 0);
                    uint8_t dg = cmd.value("default_g", 0);
                    uint8_t db = cmd.value("default_b", 0);
                    led_ctrl_->StartEffectTemporary(effect, {r, g, b}, duration_ms, hold_duration, {dr, dg, db}, frequency_hz);
                }
            } else {
                led_ctrl_->StartEffect(effect, {r, g, b}, 0, frequency_hz);
            }
        } else {
            std::cout << LOG_PREFIX << " [LED Executor] (mock) StartEffect skipped" << std::endl;
        }
    }
    else if (action == "set_led_color") {
        uint8_t r = cmd.value("r", 0);
        uint8_t g = cmd.value("g", 0);
        uint8_t b = cmd.value("b", 0);
        uint32_t duration_ms = cmd.value("duration_ms", 0);
        
        if (duration_ms > 0) {
            std::cout << LOG_PREFIX << " [LED Executor] ✓ Executing: set_color rgb=("
                      << static_cast<int>(r) << "," << static_cast<int>(g) << "," << static_cast<int>(b)
                      << ") duration=" << duration_ms << "ms (will auto-stop after timeout)" << std::endl;
            
            if (led_ctrl_) {
                led_ctrl_->SetColorTemporary(r, g, b, duration_ms, 0, 0, 0);
            }
        } else {
            std::cout << LOG_PREFIX << " [LED Executor] ✓ Executing: set_color rgb=("
                      << static_cast<int>(r) << "," << static_cast<int>(g) << "," << static_cast<int>(b)
                      << ") (permanent)" << std::endl;
            
            if (led_ctrl_) {
                led_ctrl_->SetColor(r, g, b);
            } else {
                std::cout << LOG_PREFIX << " [LED Executor] (mock) SetColor skipped" << std::endl;
            }
        }
    }
}

void DriveService::execute_servo_command(const json& cmd) {
    std::string action = cmd.value("action", "");

    if (action == "set_angle") {
        std::string channel = cmd.value("channel", "both");
        float angle = cmd.value("angle", 90.0f);
        uint8_t speed = cmd.value("speed", 50);
        
        // P1.3 Auto-hold parameters
        bool en_autohold = cmd.value("en_autohold", false);
        if (cmd.contains("en_servo_autohold")) {
             auto& val = cmd["en_servo_autohold"];
             if (val.is_boolean()) en_autohold = val.get<bool>();
             else if (val.is_number()) en_autohold = (val.get<int>() != 0);
        }
        
        int autohold_ms = cmd.value("autohold_duration", 0);
        if (cmd.contains("servo_autohold_duration")) autohold_ms = cmd.value("servo_autohold_duration", 0);

        std::cout << LOG_PREFIX << " [Servo Executor] ✓ Executing: set_angle ch=" << channel
              << " angle=" << angle << " speed=" << static_cast<int>(speed)
              << " autohold=" << (en_autohold ? "enabled" : "disabled")
              << " duration=" << autohold_ms << "ms" << std::endl;
        
        // 在执行动作前设置自动保持参数
        if (servo_ctrl_ && en_autohold) {
            if (channel == "left" || channel == "both") {
                servo_ctrl_->SetAutoHold(SERVO_LEFT, en_autohold, autohold_ms);
            }
            if (channel == "right" || channel == "both") {
                servo_ctrl_->SetAutoHold(SERVO_RIGHT, en_autohold, autohold_ms);
            }
        }
        
        if (servo_ctrl_) {
            if (channel == "left") {
                servo_ctrl_->SetAngle(SERVO_LEFT, angle, speed);
            } else if (channel == "right") {
                servo_ctrl_->SetAngle(SERVO_RIGHT, angle, speed);
            } else if (channel == "both") {
                servo_ctrl_->SetAngle(SERVO_LEFT, angle, speed);
                servo_ctrl_->SetAngle(SERVO_RIGHT, angle, speed);
            }
        } else {
            std::cout << LOG_PREFIX << " [Servo Executor] (mock) SetAngle skipped" << std::endl;
        }
    } else if (action == "enable_servo") {
        std::string channel = cmd.value("channel", "both");
        bool value = cmd.value("value", true);
        
        std::cout << LOG_PREFIX << " [Servo Executor] ✓ Executing: enable_servo ch=" << channel
              << " value=" << value << std::endl;
        
        if (channel == "left") {
            hw_service_->enable_servo_left(value);
        } else if (channel == "right") {
            hw_service_->enable_servo_right(value);
        } else if (channel == "both") {
            hw_service_->enable_servo_both(value);
        }
    } else if (action == "set_servo_multi") {
        if (cmd.contains("targets")) {
            auto targets = cmd["targets"];
            int speed = cmd.value("speed", 50);
            
            bool en_autohold = false;
            if (cmd.contains("en_servo_autohold")) {
                 auto& val = cmd["en_servo_autohold"];
                 if (val.is_boolean()) en_autohold = val.get<bool>();
                 else if (val.is_number()) en_autohold = (val.get<int>() != 0);
            }
            int autohold_ms = cmd.value("servo_autohold_duration", 3000);
            
            std::cout << LOG_PREFIX << " [Servo Executor] ✓ Executing: set_servo_multi speed=" << speed
                      << " autohold=" << (en_autohold ? "enabled" : "disabled")
                      << " duration=" << autohold_ms << "ms" << std::endl;
            
            std::map<ServoChannel, float> target_map;
            if (targets.contains("left")) {
                float angle = targets["left"].get<float>();
                target_map[SERVO_LEFT] = angle;
            }
            if (targets.contains("right")) {
                float angle = targets["right"].get<float>();
                target_map[SERVO_RIGHT] = angle;
            }
            
            // 在执行动作前设置自动保持参数
            if (servo_ctrl_ && en_autohold) {
                servo_ctrl_->SetAutoHold(SERVO_LEFT, en_autohold, autohold_ms);
                servo_ctrl_->SetAutoHold(SERVO_RIGHT, en_autohold, autohold_ms);
            }
            
            if (cmd.contains("duration")) {
                int duration = cmd["duration"].get<int>();
                if (servo_ctrl_) servo_ctrl_->MoveMultiDuration(target_map, duration);
            } else {
                if (servo_ctrl_) servo_ctrl_->MoveMulti(target_map, speed);
            }
        }
    } else if (action == "servo_swing") {
        std::string ch_str = cmd.value("channel", "left");
        float min_angle = cmd.value("min", 30.0f);
        float max_angle = cmd.value("max", 150.0f);
        int duration = cmd.value("duration", 500);
        int count = cmd.value("count", 3);
        
        ServoChannel ch;
        if (ch_str == "left") ch = SERVO_LEFT;
        else if (ch_str == "right") ch = SERVO_RIGHT;
        else return;
        
                if (servo_ctrl_) servo_ctrl_->StartSwing(ch, min_angle, max_angle, duration, count);
    } else if (action == "servo_swing_of") {
        std::string ch_str = cmd.value("channel", "left");
        float target_angle = cmd.value("target", 90.0f);
        int approach_speed = std::clamp(cmd.value("approach_speed", 50), 1, 100);
        float amplitude = std::abs(cmd.value("amplitude", 30.0f));
        int swing_speed = std::clamp(cmd.value("swing_speed", 40), 1, 100);
        int count = cmd.value("count", -1);
        
        ServoChannel ch;
        if (ch_str == "left") ch = SERVO_LEFT;
        else if (ch_str == "right") ch = SERVO_RIGHT;
        else return;
        
                if (servo_ctrl_) servo_ctrl_->ServoSwingOf(ch, target_angle, static_cast<uint8_t>(approach_speed), 
                                  amplitude, static_cast<uint8_t>(swing_speed), count);
    } else if (action == "lift_dumbbell") {
        std::string ch_str = cmd.value("channel", "left");
        float weight = cmd.value("weight", 30.0f);
        int reps = cmd.value("reps", 5);
        
        ServoChannel ch;
        if (ch_str == "left") ch = SERVO_LEFT;
        else if (ch_str == "right") ch = SERVO_RIGHT;
        else return;
        
        std::thread([this, ch, weight, reps]() {
            servo_ctrl_->LiftDumbbell(ch, weight, reps);
        }).detach();
    } else if (action == "dumbbell_dance") {
        float weight = cmd.value("weight", 30.0f);
        float duration = cmd.value("duration", 10.0f);
        
        std::thread([this, weight, duration]() {
            servo_ctrl_->DumbbellDance(weight, duration);
        }).detach();
    } else if (action == "wave_flag") {
        std::string ch_str = cmd.value("channel", "left");
        float flag_weight = cmd.value("weight", 20.0f);
        int wave_count = cmd.value("count", 10);
        
        ServoChannel ch;
        if (ch_str == "left") ch = SERVO_LEFT;
        else if (ch_str == "right") ch = SERVO_RIGHT;
        else return;
        
        std::thread([this, ch, flag_weight, wave_count]() {
            servo_ctrl_->WaveFlag(ch, flag_weight, wave_count);
        }).detach();
    } else if (action == "beat_drum") {
        std::string ch_str = cmd.value("channel", "left");
        float stick_weight = cmd.value("weight", 15.0f);
        int beat_count = cmd.value("count", 8);
        
        ServoChannel ch;
        if (ch_str == "left") ch = SERVO_LEFT;
        else if (ch_str == "right") ch = SERVO_RIGHT;
        else return;
        
        std::thread([this, ch, stick_weight, beat_count]() {
            servo_ctrl_->BeatDrum(ch, stick_weight, beat_count);
        }).detach();
    } else if (action == "paddle_row") {
        std::string ch_str = cmd.value("channel", "both");
        float paddle_weight = cmd.value("weight", 40.0f);
        int stroke_count = cmd.value("count", 6);
        
        if (ch_str == "both") {
            std::thread([this, paddle_weight, stroke_count]() {
                servo_ctrl_->DualPaddleRow(paddle_weight, stroke_count);
            }).detach();
        } else {
            ServoChannel ch;
            if (ch_str == "left") ch = SERVO_LEFT;
            else if (ch_str == "right") ch = SERVO_RIGHT;
            else return;
            
            std::thread([this, ch, paddle_weight, stroke_count]() {
                servo_ctrl_->PaddleRow(ch, paddle_weight, stroke_count);
            }).detach();
        }
    } else if (action == "servo_stop") {
        std::string ch_str = cmd.value("channel", "all");
        if (servo_ctrl_) {
            if (ch_str == "left") servo_ctrl_->Stop(SERVO_LEFT);
            else if (ch_str == "right") servo_ctrl_->Stop(SERVO_RIGHT);
            else servo_ctrl_->StopAll();
        } else {
            std::cout << LOG_PREFIX << " [Servo Executor] (mock) Stop skipped" << std::endl;
        }
    }
}

void DriveService::execute_motor_command(const json& cmd) {
    std::string action = cmd.value("action", "");

    if (action == "motor_forward") {
        float speed = cmd.value("speed", 0.5f);
        float duration = cmd.value("duration", 0.0f);
        std::cout << LOG_PREFIX << " [Motor Executor] ✓ Executing: forward speed=" << speed
              << " dur=" << duration << "s" << std::endl;
        if (motor_ctrl_) {
            motor_ctrl_->forward(speed, duration);
        } else {
            std::cout << LOG_PREFIX << " [Motor Executor] (mock) forward skipped" << std::endl;
        }
    } else if (action == "motor_backward") {
        float speed = cmd.value("speed", 0.5f);
        float duration = cmd.value("duration", 0.0f);
        std::cout << LOG_PREFIX << " [Motor Executor] ✓ Executing: backward speed=" << speed
              << " dur=" << duration << "s" << std::endl;
        if (motor_ctrl_) {
            motor_ctrl_->backward(speed, duration);
        } else {
            std::cout << LOG_PREFIX << " [Motor Executor] (mock) backward skipped" << std::endl;
        }
    } else if (action == "motor_turn_left") {
        float speed = cmd.value("speed", 0.5f);
        float duration = cmd.value("duration", 0.0f);
        std::cout << LOG_PREFIX << " [Motor Executor] ✓ Executing: turn_left speed=" << speed
              << " dur=" << duration << "s" << std::endl;
        if (motor_ctrl_) {
            motor_ctrl_->turnLeft(speed, duration);
        } else {
            std::cout << LOG_PREFIX << " [Motor Executor] (mock) turnLeft skipped" << std::endl;
        }
    } else if (action == "motor_turn_right") {
        float speed = cmd.value("speed", 0.5f);
        float duration = cmd.value("duration", 0.0f);
        std::cout << LOG_PREFIX << " [Motor Executor] ✓ Executing: turn_right speed=" << speed
              << " dur=" << duration << "s" << std::endl;
        if (motor_ctrl_) {
            motor_ctrl_->turnRight(speed, duration);
        } else {
            std::cout << LOG_PREFIX << " [Motor Executor] (mock) turnRight skipped" << std::endl;
        }
    } else if (action == "motor_stop") {
        std::cout << LOG_PREFIX << " [Motor Executor] ✓ Executing: stop" << std::endl;
        if (motor_ctrl_) {
            motor_ctrl_->stop();
        } else {
            std::cout << LOG_PREFIX << " [Motor Executor] (mock) stop skipped" << std::endl;
        }
    } else if (action == "motor_brake") {
        std::cout << LOG_PREFIX << " [Motor Executor] ✓ Executing: emergency brake" << std::endl;
        if (motor_ctrl_) {
            motor_ctrl_->brake();
        } else {
            std::cout << LOG_PREFIX << " [Motor Executor] (mock) brake skipped" << std::endl;
        }
    } else if (action == "set_motor_speed") {
        float left = cmd.value("left", 0.0f);
        float right = cmd.value("right", 0.0f);
        float duration = cmd.value("duration", 0.0f);
        std::cout << LOG_PREFIX << " [Motor Executor] ✓ Executing: set_speed L=" << left
              << " R=" << right << " dur=" << duration << "s" << std::endl;
        if (motor_ctrl_) {
            motor_ctrl_->setSpeeds(left, right, duration);
        } else {
            std::cout << LOG_PREFIX << " [Motor Executor] (mock) setSpeeds skipped" << std::endl;
        }
    } else if (action == "motor_move_pulses") {
        long pulses = cmd.value("pulses", 0L);
        float throttle = cmd.value("throttle", 0.5f);
        double assume_rate = cmd.value("assume_rate", 100.0);
        std::cout << LOG_PREFIX << " [Motor Executor] ✓ Executing: move_pulses=" << pulses
              << " throttle=" << throttle << std::endl;
        double timeout = cmd.value("timeout", 3.0);

        if (motor_ctrl_) {
            auto result = motor_ctrl_->movePulses(pulses, throttle, assume_rate, timeout);
            std::cout << LOG_PREFIX << " ✓ motor_move_pulses complete: reached="
                      << (result.reached ? "YES" : "NO")
                      << " L=" << result.left_pulses
                      << " R=" << result.right_pulses
                      << " time=" << std::fixed << std::setprecision(2) << result.elapsed_time << "s"
                      << std::defaultfloat << std::endl;
        } else {
            std::cout << LOG_PREFIX << " [Motor Executor] (mock) movePulses skipped" << std::endl;
        }
    } else if (action == "move_distance_cm") {
        // M2 新增：按距离移动（cm）
        float distance_cm = cmd.value("distance_cm", 10.0f);
        float throttle = cmd.value("throttle", 0.5f);
        uint32_t timeout = cmd.value("timeout_ms", 5000U);
        
        std::cout << LOG_PREFIX << " [Motor Executor] ✓ Executing: move_distance_cm=" << distance_cm
              << " throttle=" << throttle << " timeout=" << timeout << "ms" << std::endl;
        
        if (motor_ctrl_) {
            bool result = motor_ctrl_->move_distance_cm(distance_cm, throttle, timeout);
            std::cout << LOG_PREFIX << " ✓ move_distance_cm complete: success="
                      << (result ? "YES" : "NO") << std::endl;
        } else {
            std::cout << LOG_PREFIX << " [Motor Executor] (mock) move_distance_cm skipped" << std::endl;
        }
    } else if (action == "go_xy") {
        int16_t x = static_cast<int16_t>(cmd.value("x", 0));
        int16_t y = static_cast<int16_t>(cmd.value("y", 0));
        int speed = cmd.value("speed", 20);
        bool to_forward = cmd.value("to_forward", cmd.value("toForward", true));
        bool with_brake = cmd.value("with_brake", false);
        uint8_t acceleration_interval = static_cast<uint8_t>(cmd.value("acceleration_interval", cmd.value("accel_interval", 0)));
        bool control_speed = cmd.value("control_speed", false);
        bool control_force = cmd.value("control_force", true);
        uint32_t timeout_ms = cmd.value("timeout_ms", 10000U);

        std::cout << LOG_PREFIX << " [Motor Executor] ✓ Executing: go_xy=(" << x << "," << y
                  << ") speed=" << speed << "% toForward=" << (to_forward ? "true" : "false")
                  << " brake=" << (with_brake ? "true" : "false")
                  << " accel=" << static_cast<int>(acceleration_interval)
                  << " control_speed=" << (control_speed ? "true" : "false")
                  << " control_force=" << (control_force ? "true" : "false")
                  << " timeout=" << timeout_ms << "ms" << std::endl;

        if (motor_ctrl_) {
            bool result = motor_ctrl_->go_xy(x, y, speed, to_forward, with_brake,
                                             acceleration_interval, control_speed,
                                             control_force, timeout_ms);
            std::cout << LOG_PREFIX << " ✓ go_xy complete: success="
                      << (result ? "YES" : "NO") << std::endl;
        } else {
            std::cout << LOG_PREFIX << " [Motor Executor] (mock) go_xy skipped" << std::endl;
        }
    } else if (action == "turn_deg") {
        // M2 新增：按角度转向（度数）
        float angle_deg = cmd.value("angle_deg", 90.0f);
        float throttle = cmd.value("throttle", 0.5f);
        int speed = cmd.value("speed", -1);
        bool from_center = cmd.value("from_center", cmd.value("is_center", cmd.value("isCenter", true)));
        bool to_forward = cmd.value("to_forward", cmd.value("toForward", angle_deg >= 0.0f));
        bool with_brake = cmd.value("with_brake", true);
        uint8_t acceleration_interval = static_cast<uint8_t>(cmd.value("acceleration_interval", cmd.value("accel_interval", 0)));
        bool control_speed = cmd.value("control_speed", false);
        bool control_force = cmd.value("control_force", true);
        uint32_t timeout = cmd.value("timeout_ms", 5000U);

        int resolved_speed = speed;
        if (resolved_speed < 0) {
            resolved_speed = static_cast<int>(std::lround(std::min(100.0f, std::abs(throttle) * 150.0f)));
        }
        if (resolved_speed > 0 && resolved_speed < 25) {
            resolved_speed = 25;
        }
        
        std::cout << LOG_PREFIX << " [Motor Executor] ✓ Executing: turn_deg=" << angle_deg
              << " throttle=" << throttle << " resolved_speed=" << resolved_speed
              << "% from_center=" << (from_center ? "true" : "false")
              << " toForward=" << (to_forward ? "true" : "false")
              << " brake=" << (with_brake ? "true" : "false")
              << " accel=" << static_cast<int>(acceleration_interval)
              << " control_speed=" << (control_speed ? "true" : "false")
              << " control_force=" << (control_force ? "true" : "false")
              << " timeout=" << timeout << "ms" << std::endl;
        
        if (motor_ctrl_) {
            bool result = motor_ctrl_->go_rotate(angle_deg, from_center, resolved_speed,
                                                 to_forward, with_brake, acceleration_interval,
                                                 control_speed, control_force, timeout);
            std::cout << LOG_PREFIX << " ✓ turn_deg complete: success="
                      << (result ? "YES" : "NO") << std::endl;
        } else {
            std::cout << LOG_PREFIX << " [Motor Executor] (mock) turn_deg skipped" << std::endl;
        }
    } else if (action == "move_distance_cm_pid") {
        // M2 新增：基于 PID 的精确距离移动
        // 兼容两个距离字段名：distance_cm 或 distance
        float distance_cm = cmd.value("distance_cm", cmd.value("distance", 10.0f));
        float speed = cmd.value("speed", 0.5f);
        int direction = cmd.value("direction", 0);  // 0=后退, 1=前进
        uint32_t timeout_ms = cmd.value("timeout_ms", 5000U);
        
        // 单位转换：如果 speed > 1.0，认为是百分比（1-100），需要转成 0..1
        float speed_normalized = (speed > 1.0f) ? (speed / 100.0f) : speed;
        
        // 方向处理：direction=1 是前进（正距离），direction=0 是后退（负距离）
        float resolved_distance_cm = distance_cm;
        std::string direction_str = "FWD";
        if (direction == 0) {  // direction=0 -> 后退
            resolved_distance_cm = -std::abs(distance_cm);
            direction_str = "BWD";
        } else {  // direction=1 或其他 -> 前进
            resolved_distance_cm = std::abs(distance_cm);
            direction_str = "FWD";
        }
        
        std::cout << LOG_PREFIX << " [Motor Executor] ✓ Executing: move_distance_cm_pid=" << distance_cm
              << "cm(resolved=" << resolved_distance_cm << "cm) speed=" << speed
              << "%(norm=" << speed_normalized << ") direction_input=" << direction
              << " dir=" << direction_str << " timeout=" << timeout_ms << "ms" << std::endl;
        
        if (motor_ctrl_) {
            std::cout << LOG_PREFIX << " [PID_THREAD] Starting move_distance_cm_pid with resolved_distance="
                      << resolved_distance_cm << " speed=" << speed_normalized
                      << " direction_to_controller=0 (signed distance handles direction)" << std::endl;

            bool result = motor_ctrl_->move_distance_cm_pid(resolved_distance_cm, speed_normalized, 0, timeout_ms);

            std::cout << LOG_PREFIX << " [PID_THREAD] move_distance_cm_pid complete: success="
                      << (result ? "YES" : "NO") << " resolved_distance=" << resolved_distance_cm
                      << " direction=" << direction_str << std::endl;
        } else {
            std::cout << LOG_PREFIX << " [Motor Executor] (mock) move_distance_cm_pid skipped" << std::endl;
        }
    } else if (action == "turn_deg_pid") {
        // M2 新增：基于 PID 的精确角度转向
        float angle_deg = cmd.value("angle_deg", 90.0f);
        float speed = cmd.value("speed", 0.3f);
        uint32_t timeout_ms = cmd.value("timeout_ms", 5000U);
        
        // 单位转换：如果 speed > 1.0，认为是百分比（1-100），需要转成 0..1
        float speed_normalized = (speed > 1.0f) ? (speed / 100.0f) : speed;
        
        std::cout << LOG_PREFIX << " [Motor Executor] ✓ Executing: turn_deg_pid"
              << " input=[angle=" << angle_deg << "°, speed=" << speed << "%]"
              << " resolved=[angle=" << angle_deg << "°, speed_norm=" << speed_normalized << "]"
              << " timeout=" << timeout_ms << "ms" << std::endl;
        
        if (motor_ctrl_) {
            std::cout << LOG_PREFIX << " [PID_THREAD] Starting turn_deg_pid with angle="
                      << angle_deg << "° speed=" << speed_normalized << std::endl;

            bool result = motor_ctrl_->turn_deg_pid(angle_deg, speed_normalized, timeout_ms);

            std::cout << LOG_PREFIX << " [PID_THREAD] turn_deg_pid complete: success="
                      << (result ? "YES" : "NO") << " angle=" << angle_deg << "°" << std::endl;
        } else {
            std::cout << LOG_PREFIX << " [Motor Executor] (mock) turn_deg_pid skipped" << std::endl;
        }
    } else if (action == "drive_distance") {
        // 动画系统 API：高级距离移动（完整 SDK 风格参数）
        float distance_mm = cmd.value("distance_mm", cmd.value("distance", 1000.0f));
        int speed = cmd.value("speed", 20);
        bool to_forward = cmd.value("to_forward", cmd.value("toForward", true));
        bool with_brake = cmd.value("with_brake", cmd.value("brake", false));
        uint8_t acceleration_interval = static_cast<uint8_t>(cmd.value("acceleration_interval", cmd.value("accel_interval", cmd.value("accel", 0))));
        bool control_speed = cmd.value("control_speed", false);
        bool control_force = cmd.value("control_force", true);
        uint32_t timeout_ms = cmd.value("timeout_ms", 10000U);

        if (cmd.contains("direction")) {
            int direction = cmd.value("direction", 0);
            to_forward = (direction == 0);
        }

        std::cout << LOG_PREFIX << " [Motor Executor] ✓ Executing: drive_distance=" << distance_mm
              << "mm speed=" << speed << "% toForward=" << (to_forward ? "true" : "false")
              << " brake=" << (with_brake ? "true" : "false")
              << " accel=" << static_cast<int>(acceleration_interval)
              << " control_speed=" << (control_speed ? "true" : "false")
              << " control_force=" << (control_force ? "true" : "false")
              << " timeout=" << timeout_ms << "ms" << std::endl;

        if (motor_ctrl_) {
            bool result = motor_ctrl_->go_distance(distance_mm, speed, to_forward,
                                                   with_brake, acceleration_interval,
                                                   control_speed, control_force, timeout_ms);
            std::cout << LOG_PREFIX << " ✓ drive_distance complete: success="
                      << (result ? "YES" : "NO") << std::endl;
        } else {
            std::cout << LOG_PREFIX << " [Motor Executor] (mock) drive_distance skipped" << std::endl;
        }
    } else if (action == "drive_rotate") {
        // 动画系统 API：高级转向控制
        float angle_deg = cmd.value("angle_deg", cmd.value("driveRotate", 90.0f));  // 兼容 driveRotate 参数
        int speed = cmd.value("speed", 20);
        bool is_center = cmd.value("from_center", cmd.value("is_center", cmd.value("isCenter", true)));
        bool to_forward = cmd.value("to_forward", cmd.value("toForward", angle_deg >= 0.0f));
        bool with_brake = cmd.value("with_brake", true);
        uint8_t acceleration_interval = static_cast<uint8_t>(cmd.value("acceleration_interval", cmd.value("accel_interval", 0)));
        bool control_speed = cmd.value("control_speed", false);
        bool control_force = cmd.value("control_force", true);
        uint32_t timeout_ms = cmd.value("timeout_ms", 8000U);
        int resolved_speed = speed;
        if (is_center) {
            resolved_speed = static_cast<int>(std::lround(std::abs(speed) * 1.5f));
            if (resolved_speed > 0 && resolved_speed < 25) {
                resolved_speed = 25;
            }
        }
        
        std::cout << LOG_PREFIX << " [Motor Executor] ✓ Executing: drive_rotate=" << angle_deg
              << "° speed=" << speed << "% resolved_speed=" << resolved_speed
              << "% mode=" << (is_center ? "CENTER" : "WHEEL")
              << " toForward=" << (to_forward ? "true" : "false")
              << " brake=" << (with_brake ? "true" : "false")
              << " accel=" << static_cast<int>(acceleration_interval)
              << " control_speed=" << (control_speed ? "true" : "false")
              << " control_force=" << (control_force ? "true" : "false")
              << " timeout=" << timeout_ms << "ms" << std::endl;
        
        if (motor_ctrl_) {
            bool result = motor_ctrl_->go_rotate(angle_deg, is_center, resolved_speed, to_forward,
                                                 with_brake, acceleration_interval,
                                                 control_speed, control_force, timeout_ms);
            std::cout << LOG_PREFIX << " ✓ drive_rotate complete: success="
                      << (result ? "YES" : "NO") << std::endl;
        } else {
            std::cout << LOG_PREFIX << " [Motor Executor] (mock) drive_rotate skipped" << std::endl;
        }
    } else if (action == "drive_rotate_left") {
        // 动画系统 API：左转（便捷函数）
        float angle_deg = cmd.value("angle_deg", cmd.value("driveRotate", 90.0f));
        int speed = cmd.value("speed", 20);
        bool is_center = cmd.value("is_center", cmd.value("isCenter", true));
        uint32_t timeout_ms = cmd.value("timeout_ms", 8000U);
        
        std::cout << LOG_PREFIX << " [Motor Executor] ✓ Executing: drive_rotate_left=" << angle_deg
              << "° speed=" << speed << "%" << std::endl;
        
        if (motor_ctrl_) {
            bool result = motor_ctrl_->drive_rotate_left(angle_deg, speed, is_center, timeout_ms);
            std::cout << LOG_PREFIX << " ✓ drive_rotate_left complete: success="
                      << (result ? "YES" : "NO") << std::endl;
        } else {
            std::cout << LOG_PREFIX << " [Motor Executor] (mock) drive_rotate_left skipped" << std::endl;
        }
    } else if (action == "drive_rotate_right") {
        // 动画系统 API：右转（便捷函数）
        float angle_deg = cmd.value("angle_deg", cmd.value("driveRotate", 90.0f));
        int speed = cmd.value("speed", 20);
        bool is_center = cmd.value("is_center", cmd.value("isCenter", true));
        uint32_t timeout_ms = cmd.value("timeout_ms", 8000U);
        
        std::cout << LOG_PREFIX << " [Motor Executor] ✓ Executing: drive_rotate_right=" << angle_deg
              << "° speed=" << speed << "%" << std::endl;
        
        if (motor_ctrl_) {
            bool result = motor_ctrl_->drive_rotate_right(angle_deg, speed, is_center, timeout_ms);
            std::cout << LOG_PREFIX << " ✓ drive_rotate_right complete: success="
                      << (result ? "YES" : "NO") << std::endl;
        } else {
            std::cout << LOG_PREFIX << " [Motor Executor] (mock) drive_rotate_right skipped" << std::endl;
        }
    } else if (action == "drive_distance_pid") {
        // 动画系统 API (PID 版本)：高级距离移动
        float distance_mm = cmd.value("distance_mm", cmd.value("distance", 1000.0f));
        int speed = cmd.value("speed", 20);
        int accel = cmd.value("accel", 0);
        int brake = cmd.value("brake", 0);
        int direction = cmd.value("direction", 0);
        uint32_t timeout_ms = cmd.value("timeout_ms", 10000U);
        
        std::cout << LOG_PREFIX << " [Motor Executor] ✓ Executing: drive_distance_pid=" << distance_mm
              << "mm speed=" << speed << "% dir=" << (direction == 0 ? "FWD" : "BWD")
              << " timeout=" << timeout_ms << "ms" << std::endl;
        
        if (motor_ctrl_) {
            bool result = motor_ctrl_->drive_distance_pid(distance_mm, speed, accel, brake, direction, timeout_ms);
            std::cout << LOG_PREFIX << " ✓ drive_distance_pid complete: success="
                      << (result ? "YES" : "NO") << std::endl;
        } else {
            std::cout << LOG_PREFIX << " [Motor Executor] (mock) drive_distance_pid skipped" << std::endl;
        }
    } else if (action == "turn_deg_pid_advanced") {
        // 动画系统 API (PID 版本)：高级转向控制
        float angle_deg = cmd.value("angle_deg", cmd.value("driveRotate", 90.0f));
        int speed = cmd.value("speed", 20);
        bool is_center = cmd.value("is_center", cmd.value("isCenter", true));
        uint32_t timeout_ms = cmd.value("timeout_ms", 8000U);
        
        std::cout << LOG_PREFIX << " [Motor Executor] ✓ Executing: turn_deg_pid_advanced=" << angle_deg
              << "° speed=" << speed << "% mode=" << (is_center ? "CENTER" : "WHEEL")
              << " timeout=" << timeout_ms << "ms" << std::endl;
        
        if (motor_ctrl_) {
            bool result = motor_ctrl_->turn_deg_pid_advanced(angle_deg, speed, is_center, timeout_ms);
            std::cout << LOG_PREFIX << " ✓ turn_deg_pid_advanced complete: success="
                      << (result ? "YES" : "NO") << std::endl;
        } else {
            std::cout << LOG_PREFIX << " [Motor Executor] (mock) turn_deg_pid_advanced skipped" << std::endl;
        }
    } else if (action == "sdk_init") {
        int gx = cmd.value("gx", 0);
        int gy = cmd.value("gy", 0);
        int gz = cmd.value("gz", 0);
        int ax = cmd.value("ax", 0);
        int ay = cmd.value("ay", 0);
        int az = cmd.value("az", 0);
        int8_t status = DriveControl::init(gx, gy, gz, ax, ay, az);
        std::cout << "[SDK] init() returned " << (int)status << std::endl;
    } else if (action == "sdk_dispose") {
        bool dispose_imu = cmd.value("dispose_imu", false);
        int8_t status = DriveControl::dispose(dispose_imu);
        std::cout << "[SDK] dispose() returned " << (int)status << std::endl;
    } else if (action == "sdk_is_active") {
        bool active = DriveControl::isActive();
        std::cout << "[SDK] isActive() returned " << (active ? "true" : "false") << std::endl;
    } else if (action == "sdk_abort") {
        DriveControl::Abort();
        std::cout << "[SDK] Abort() called" << std::endl;
    } else if (action == "sdk_free_drive") {
        uint8_t speed = cmd.value("speed", 50);
        bool isLeft = cmd.value("isLeft", true);
        bool toForward = cmd.value("toForward", true);
        bool accepted = DriveControl::freeDrive(speed, isLeft, toForward);
        std::cout << "[SDK] freeDrive() returned " << (accepted ? "true" : "false") << std::endl;
    } else if (action == "sdk_go_xy") {
        uint16_t id = cmd.value("id", 1000);
        int16_t x = cmd.value("x", 0);
        int16_t y = cmd.value("y", 0);
        uint8_t speed = cmd.value("speed", 50);
        bool toForward = cmd.value("toForward", true);
        bool with_brake = cmd.value("with_brake", false);
        uint8_t acc = cmd.value("accel_interval", 0);
        bool ctrl_speed = cmd.value("control_speed", false);
        bool ctrl_force = cmd.value("control_force", true);
        bool accpt = DriveControl::goXY(id, x, y, speed, toForward, with_brake, acc, ctrl_speed, ctrl_force);
        std::cout << "[SDK] goXY() returned " << (accpt ? "true" : "false") << std::endl;
    } else if (action == "sdk_go_distance") {
        uint16_t id = cmd.value("id", 1001);
        uint16_t mm = cmd.value("mm", 100);
        uint8_t speed = cmd.value("speed", 50);
        bool toForward = cmd.value("toForward", true);
        bool with_brake = cmd.value("with_brake", false);
        uint8_t acc = cmd.value("accel_interval", 0);
        bool ctrl_speed = cmd.value("control_speed", false);
        bool ctrl_force = cmd.value("control_force", true);
        bool accpt = DriveControl::goDistance(id, mm, speed, toForward, with_brake, acc, ctrl_speed, ctrl_force);
        std::cout << "[SDK] goDistance() returned " << (accpt ? "true" : "false") << std::endl;
    } else if (action == "sdk_go_rotate") {
        uint16_t id = cmd.value("id", 1002);
        float rotateAngle = cmd.value("rotateAngle", 90.0f);
        bool from_center = cmd.value("from_center", true);
        uint8_t speed = cmd.value("speed", 50);
        bool toForward = cmd.value("toForward", true);
        bool with_brake = cmd.value("with_brake", false);
        uint8_t acc = cmd.value("accel_interval", 0);
        bool ctrl_speed = cmd.value("control_speed", false);
        bool ctrl_force = cmd.value("control_force", true);
        bool accpt = DriveControl::goRotate(id, rotateAngle, from_center, speed, toForward, with_brake, acc, ctrl_speed, ctrl_force);
        std::cout << "[SDK] goRotate() returned " << (accpt ? "true" : "false") << std::endl;
    } else if (action == "sdk_get_position") {
        Position pos = DriveControl::getPosition();
        std::cout << "[SDK] getPosition() => diffx: " << pos.x << " diffy: " << pos.y << " theta: " << pos.head << std::endl;
    } else if (action == "sdk_reset_position") {
        DriveControl::resetPosition();
        std::cout << "[SDK] resetPosition() called" << std::endl;
    } else if (action == "sdk_get_state") {
        DriveState st = DriveControl::getState();
        std::cout << "[SDK] getState() returned " << (int)st << std::endl;
    } else if (action == "sdk_get_rpm") {
        bool isLeft = cmd.value("isLeft", true);
        float rpm = DriveControl::getRPM(isLeft);
        std::cout << "[SDK] getRPM(" << isLeft << ") => " << rpm << std::endl;
    } else if (action == "sdk_get_version") {
        float ver = DriveControl::getVersion();
        std::cout << "[SDK] getVersion() => " << ver << std::endl;
    } else if (action == "get_encoder_values") {
        // M2 新增：查询编码器值
        std::cout << LOG_PREFIX << " [Motor Executor] ✓ Executing: get_encoder_values" << std::endl;
        
        if (motor_ctrl_) {
            int32_t left = motor_ctrl_->get_left_encoder_value();
            int32_t right = motor_ctrl_->get_right_encoder_value();
            int32_t diff = left - right;
            
            std::cout << LOG_PREFIX << " [Encoder Values] left=" << left
                      << " right=" << right << " diff=" << diff << std::endl;
            
            // 可选：将结果发布到ZMQ回复通道（如果有的话）
        } else {
            std::cout << LOG_PREFIX << " [Motor Executor] (mock) get_encoder_values skipped" << std::endl;
        }
    }
}

} // namespace doly::drive
