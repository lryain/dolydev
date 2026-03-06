#include "fan_service_bus.h"
#include <iostream>
#include "FanControl.h"
#include <thread>
#include <chrono>
#include <fstream>
#include <ctime>
#include <vector>
#include <string>
#include <csignal>
#include <atomic>
#include <sstream>
#include <memory>
#include <algorithm>
#include <cstdint>
#include <array>
#include <cmath>
#include <map>
#include <mutex>
#include <zmq.hpp>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

extern std::atomic<bool> running; // defined later in this file

// 模式状态结构体
struct ModeState {
    std::string mode;           // "quiet", "normal", "performance"
    std::chrono::steady_clock::time_point expires;  // 过期时间
    bool is_persistent = false; // true=持久，false=临时有效期
};

// 模式-specific PWM 配置
struct ModePwmConfig {
    int pwm_min;  // 该模式下的最小 PWM
    int pwm_max;  // 该模式下的最大 PWM
};

// ZMQ / Bus related globals
static std::atomic<int> g_manual_pwm(-1); // -1 = no manual override
static std::atomic<bool> g_fan_enabled(true);
static std::mutex g_inhibit_mutex;
static std::map<std::string, std::chrono::steady_clock::time_point> g_inhibits; // id -> expiry (steady_clock::time_point::max() == indefinite)
static std::mutex g_manual_mutex;
static std::chrono::steady_clock::time_point g_manual_override_expiry = std::chrono::steady_clock::time_point::min();
static std::atomic<double> g_mode_scale_max(1.0);
static std::atomic<double> g_mode_scale_min(1.0);
// 当前实际写入的 PWM（用于发布到 ZMQ）
static std::atomic<int> g_current_pwm(0);
// 模式状态（持久或临时）
static std::mutex g_mode_mutex;
static ModeState g_mode_state = {"normal", std::chrono::steady_clock::time_point::max(), false};

// 模式-specific PWM 配置（从配置文件加载）
static std::mutex g_mode_pwm_mutex;
static std::map<std::string, ModePwmConfig> g_mode_pwm_config = {
    {"quiet", {1500, 3095}},        // 默认值
    {"normal", {1500, 3095}},
    {"performance", {1500, 3095}}
};

inline uint64_t nowMs() {
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count());
}

static void addInhibit(const std::string& id, int duration_ms, std::ostream* log) {
    std::lock_guard<std::mutex> lock(g_inhibit_mutex);
    auto expiry = (duration_ms <= 0) ? std::chrono::steady_clock::time_point::max()
                                     : (std::chrono::steady_clock::now() + std::chrono::milliseconds(duration_ms));
    g_inhibits[id] = expiry;
    if (log) (*log) << "[ZMQ] Added inhibit '" << id << "' duration_ms=" << duration_ms << " at " << nowMs() << std::endl;
}

static void removeInhibit(const std::string& id, std::ostream* log) {
    std::lock_guard<std::mutex> lock(g_inhibit_mutex);
    auto it = g_inhibits.find(id);
    if (it != g_inhibits.end()) {
        g_inhibits.erase(it);
        if (log) (*log) << "[ZMQ] Removed inhibit '" << id << "' at " << nowMs() << std::endl;
    }
}

static bool anyInhibitActive() {
    std::lock_guard<std::mutex> lock(g_inhibit_mutex);
    auto now = std::chrono::steady_clock::now();
    // clean expired
    for (auto it = g_inhibits.begin(); it != g_inhibits.end();) {
        if (it->second != std::chrono::steady_clock::time_point::max() && it->second <= now) {
            it = g_inhibits.erase(it);
        } else {
            ++it;
        }
    }
    return !g_inhibits.empty();
}

static void setManualPwm(int pwm, int duration_ms, std::ostream* log) {
    g_manual_pwm.store(pwm);
    {
        std::lock_guard<std::mutex> lock(g_manual_mutex);
        g_manual_override_expiry = (duration_ms <= 0) ? std::chrono::steady_clock::time_point::max()
                                                      : (std::chrono::steady_clock::now() + std::chrono::milliseconds(duration_ms));
    }
    if (log) (*log) << "[ZMQ] Manual PWM set to " << pwm << " duration_ms=" << duration_ms << " at " << nowMs() << std::endl;
}

static bool manualOverrideActive() {
    int pwm = g_manual_pwm.load();
    if (pwm < 0) return false;
    std::lock_guard<std::mutex> lock(g_manual_mutex);
    if (g_manual_override_expiry == std::chrono::steady_clock::time_point::max()) return true;
    return std::chrono::steady_clock::now() <= g_manual_override_expiry;
}

// 检查模式是否有效（持久或未过期）
static bool isModeActive() {
    std::lock_guard<std::mutex> lock(g_mode_mutex);
    if (g_mode_state.is_persistent) {
        return true; // 持久模式一直有效
    }
    if (g_mode_state.expires == std::chrono::steady_clock::time_point::max()) {
        return false; // 非持久且无过期时间 = 无效
    }
    return std::chrono::steady_clock::now() <= g_mode_state.expires;
}

// 获取当前有效的模式字符串
static std::string getCurrentMode() {
    std::lock_guard<std::mutex> lock(g_mode_mutex);
    return g_mode_state.mode;
}

// 设置持久模式（一直保持，直到收到新命令）
static void setPersistentMode(const std::string& mode, std::ostream* log) {
    std::lock_guard<std::mutex> lock(g_mode_mutex);
    g_mode_state.mode = mode;
    g_mode_state.expires = std::chrono::steady_clock::time_point::max();
    g_mode_state.is_persistent = true;
    if (log) (*log) << "[ZMQ] Persistent mode set to '" << mode << "' at " << nowMs() << std::endl;
}

// 设置临时模式（指定时间后回到自动控制）
static void setTemporaryMode(const std::string& mode, int duration_ms, std::ostream* log) {
    std::lock_guard<std::mutex> lock(g_mode_mutex);
    g_mode_state.mode = mode;
    if (duration_ms > 0) {
        g_mode_state.expires = std::chrono::steady_clock::now() + std::chrono::milliseconds(duration_ms);
    } else {
        g_mode_state.expires = std::chrono::steady_clock::time_point::max();
    }
    g_mode_state.is_persistent = false;
    if (log) (*log) << "[ZMQ] Temporary mode set to '" << mode << "' duration_ms=" << duration_ms << " at " << nowMs() << std::endl;
}

// 清除模式（回到自动控制）
static void clearMode(std::ostream* log) {
    std::lock_guard<std::mutex> lock(g_mode_mutex);
    g_mode_state.mode = "normal";
    g_mode_state.expires = std::chrono::steady_clock::time_point::min();
    g_mode_state.is_persistent = false;
    if (log) (*log) << "[ZMQ] Mode cleared, back to auto control at " << nowMs() << std::endl;
}

// 根据模式字符串返回该模式的 PWM 范围
static ModePwmConfig getModePwmRange(const std::string& mode) {
    std::lock_guard<std::mutex> lock(g_mode_pwm_mutex);
    auto it = g_mode_pwm_config.find(mode);
    if (it != g_mode_pwm_config.end()) {
        return it->second;
    }
    // 如果未找到，返回 normal 模式的值
    return g_mode_pwm_config["normal"];
}

static json wrapEnvelope(const json& data) {
    json env;
    env["ts"] = nowMs();
    env["src"] = "fan_service";
    env["data"] = data;
    return env;
}

// Command thread: subscribes to cmd.fan.* topics and updates local state.
// 注意：服务端 bind，客户端 connect
static void commandThread(const std::string endpoint, std::ostream* log) {
    try {
        zmq::context_t ctx(1);
        zmq::socket_t sub_sock(ctx, zmq::socket_type::sub);
        sub_sock.setsockopt(ZMQ_LINGER, 0);
        sub_sock.bind(endpoint);  // 改为 bind：服务提供者绑定端点

        const char* prefix = "cmd.fan.";
        sub_sock.setsockopt(ZMQ_SUBSCRIBE, prefix, std::strlen(prefix));
        sub_sock.setsockopt(ZMQ_RCVTIMEO, 250);

        while (running.load()) {
            try {
                zmq::message_t topic_msg;
                if (!sub_sock.recv(topic_msg)) continue; // timeout
                std::string topic(static_cast<char*>(topic_msg.data()), topic_msg.size());

                zmq::message_t payload_msg;
                if (!sub_sock.recv(payload_msg)) continue;
                std::string payload(static_cast<char*>(payload_msg.data()), payload_msg.size());

                json message;
                try {
                    message = json::parse(payload);
                } catch (const json::exception& e) {
                    if (log) (*log) << "[ZMQ] JSON parse error: " << e.what() << " payload=" << payload << std::endl;
                    continue;
                }

                json data = message.contains("data") ? message["data"] : message;

                if (topic == "cmd.fan.set") {
                    if (data.contains("pwm")) {
                        int pwm = data.value("pwm", 0);
                        int dur = data.value("duration_ms", 0);
                        setManualPwm(pwm, dur, log);
                    } else if (data.contains("pct") || data.contains("percent") || data.contains("speed")) {
                        double pct = data.value("pct", data.value("percent", data.value("speed", 0.0)));
                        pct = std::clamp(pct, 0.0, 1.0);
                        int dur = data.value("duration_ms", 0);
                        // pwm range 0-4095
                        int pwm = static_cast<int>(std::round(pct * 4095.0));
                        setManualPwm(pwm, dur, log);
                    }
                } else if (topic == "cmd.fan.inhibit") {
                    std::string id = data.value("id", std::string("audio"));
                    int dur = data.value("duration_ms", 0);
                    addInhibit(id, dur, log);
                } else if (topic == "cmd.fan.uninhibit" || topic == "cmd.fan.clear_inhibit") {
                    std::string id = data.value("id", std::string());
                    if (!id.empty()) removeInhibit(id, log);
                } else if (topic == "cmd.fan.enable") {
                    bool enabled = data.value("enabled", true);
                    g_fan_enabled.store(enabled);
                    if (log) (*log) << "[ZMQ] Fan enabled set to " << enabled << " at " << nowMs() << std::endl;
                } else if (topic == "cmd.fan.persistent-mode") {
                    // 设置持久模式：模式将一直保持，直到收到新命令
                    std::string mode = data.value("mode", std::string("normal"));
                    setPersistentMode(mode, log);
                } else if (topic == "cmd.fan.mode") {
                    // 设置临时模式：如果指定了 duration_ms，则该时间后回到自动控制；
                    // 如果没指定 duration_ms，则一直保持此模式
                    std::string mode = data.value("mode", std::string("normal"));
                    int dur = data.value("duration_ms", 0);
                    // 如果没指定 duration_ms，则视为持久
                    if (dur <= 0) {
                        setPersistentMode(mode, log);
                    } else {
                        setTemporaryMode(mode, dur, log);
                    }
                } else if (topic == "cmd.fan.clear-mode" || topic == "cmd.fan.clearmode") {
                    // 清除模式，回到自动控制
                    clearMode(log);
                } else {
                    if (log) (*log) << "[ZMQ] Unhandled topic " << topic << " payload=" << payload << std::endl;
                }

            } catch (const zmq::error_t& e) {
                if (e.num() == ETERM || e.num() == EINTR) break;
                if (log) (*log) << "[ZMQ] commandThread error: " << e.what() << std::endl;
            }
        }

    } catch (const zmq::error_t& e) {
        if (log) (*log) << "[ZMQ] Failed to start command subscriber: " << e.what() << std::endl;
    }
}

// Status thread: publishes `status.fan.state` periodically
// 注意：服务端 bind SUB（接收命令），客户端们 connect 到接收端点
// 状态发布使用另一个端点，或者在同一端点上使用不同的 topic 前缀
static void statusThread(const std::string endpoint, int interval_ms, std::ostream* log) {
    try {
        zmq::context_t ctx(1);
        zmq::socket_t pub_sock(ctx, zmq::socket_type::pub);
        pub_sock.setsockopt(ZMQ_LINGER, 0);
        // 状态发布 bind 到原端点 bind 的备用端点（通常服务有多个端点）
        // 为了简单起见，我们让 statusThread 也 connect 到同一个端点（作为发布者）
        // 但实际上这会导致地址冲突。最好的做法是使用不同的端点或让 commandThread bind
        // 这里我们改为：commandThread bind，statusThread 也通过消息队列方式工作
        // 或者使用 inproc:// 和一个代理线程
        
        // 为避免端口冲突，我们让 statusThread 也 connect（代替 bind）
        // 假设 commandThread 已经 bind，这样 statusThread connect 会失败
        // 解决方案：使用 XPUB/XSUB 或改用同一个 SUB socket 处理所有消息
        
        // 临时修复：使用 inproc:// 通道或延迟启动
        // 最简洁的方案：让 statusThread 不 bind，改为通过全局变量触发消息的方式
        // 或者状态信息通过另一个 socket 发送
        
        // 快速修复：给 status 一个不同的端点
        std::string status_endpoint = endpoint;
        // 替换 sock 为 sock_status
        size_t sock_pos = status_endpoint.find("doly_zmq");
        if (sock_pos != std::string::npos) {
            status_endpoint = status_endpoint.substr(0, sock_pos) + "doly_zmq_status" + 
                             status_endpoint.substr(sock_pos + 8);
        }
        
        pub_sock.bind(status_endpoint);  // bind 到不同的端点

        while (running.load()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(interval_ms));

            json status;
            status["enabled"] = g_fan_enabled.load();
            int manual = g_manual_pwm.load();
            status["manual_override"] = (manual >= 0) && manualOverrideActive();
            if (manual >= 0) status["manual_pwm"] = manual; else status["manual_pwm"] = nullptr;
            status["inhibited"] = anyInhibitActive();

            {
                std::lock_guard<std::mutex> lock(g_inhibit_mutex);
                json arr = json::array();
                for (const auto& [id, expiry] : g_inhibits) {
                    json item;
                    item["id"] = id;
                    if (expiry == std::chrono::steady_clock::time_point::max()) {
                        item["expires_ms"] = nullptr;
                    } else {
                        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(expiry.time_since_epoch()).count();
                        item["expires_ms"] = ms;
                    }
                    arr.push_back(item);
                }
                status["inhibits"] = arr;
            }

            // 添加模式信息
            {
                std::lock_guard<std::mutex> lock(g_mode_mutex);
                status["mode"] = g_mode_state.mode;
                status["mode_persistent"] = g_mode_state.is_persistent;
                if (g_mode_state.expires != std::chrono::steady_clock::time_point::max() &&
                    g_mode_state.expires != std::chrono::steady_clock::time_point::min()) {
                    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(g_mode_state.expires.time_since_epoch()).count();
                    status["mode_expires_ms"] = ms;
                } else {
                    status["mode_expires_ms"] = nullptr;
                }
            }
            auto pwm_config = getModePwmRange(getCurrentMode());
            status["mode_pwm_min"] = pwm_config.pwm_min;
            status["mode_pwm_max"] = pwm_config.pwm_max;

            json env = wrapEnvelope(status);
            std::string payload = env.dump();

            try {
                pub_sock.send(zmq::buffer(std::string("status.fan.state")), zmq::send_flags::sndmore);
                pub_sock.send(zmq::buffer(payload), zmq::send_flags::none);
            } catch (const zmq::error_t& e) {
                if (log) (*log) << "[ZMQ] status publish error: " << e.what() << std::endl;
            }
        }
    } catch (const zmq::error_t& e) {
        if (log) (*log) << "[ZMQ] Failed to start status publisher: " << e.what() << std::endl;
    }
}

// Null streambuf 用于禁用日志输出
class NullBuf : public std::streambuf {
public:
    int overflow(int c) override { return c; }
};

// Fan speed publish thread：根据配置决定是否发布当前 PWM 到 ZMQ
static std::atomic<int> g_publish_fan_speed_enabled(0);
static std::atomic<int> g_publish_fan_speed_interval_sec(5);

static void fanSpeedPublishThread(const std::string endpoint, std::ostream* log) {
    try {
        zmq::context_t ctx(1);
        zmq::socket_t pub_sock(ctx, zmq::socket_type::pub);
        pub_sock.setsockopt(ZMQ_LINGER, 0);

        std::string speed_endpoint = endpoint;
        size_t sock_pos = speed_endpoint.find("doly_zmq");
        if (sock_pos != std::string::npos) {
            speed_endpoint = speed_endpoint.substr(0, sock_pos) + "doly_zmq_speed" + speed_endpoint.substr(sock_pos + 8);
        }
        pub_sock.bind(speed_endpoint);

        while (running.load()) {
            int enabled = g_publish_fan_speed_enabled.load();
            int interval = g_publish_fan_speed_interval_sec.load();
            if (enabled) {
                // publish current pwm
                json payload;
                payload["ts"] = nowMs();
                payload["pwm"] = g_current_pwm.load();
                std::string msg = payload.dump();
                try {
                    pub_sock.send(zmq::buffer(std::string("status.fan.speed")), zmq::send_flags::sndmore);
                    pub_sock.send(zmq::buffer(msg), zmq::send_flags::none);
                } catch (const zmq::error_t& e) {
                    if (log) (*log) << "[ZMQ] fanSpeed publish error: " << e.what() << std::endl;
                }
            }
            // sleep for interval seconds (minimum 1s)
            std::this_thread::sleep_for(std::chrono::seconds(std::max(1, interval)));
        }
    } catch (const zmq::error_t& e) {
        if (log) (*log) << "[ZMQ] Failed to start fan speed publisher: " << e.what() << std::endl;
    }
}

class Config {
public:
    static std::vector<uint16_t> loadPwmValues(const std::string& configFile) {
        std::vector<uint16_t> values;
        std::ifstream file(configFile);
        if (!file.is_open()) {
            std::cerr << "Failed to open config file: " << configFile << std::endl;
            return values;
        }
        std::string line;
        bool inPwmSection = true;
        while (std::getline(file, line)) {
            if (line.empty()) continue;
            if (line[0] == '#') {
                if (line.find("# Temperature config") != std::string::npos) {
                    inPwmSection = false;
                }
                continue;
            }
            if (!inPwmSection) continue;
            try {
                uint16_t value = std::stoi(line);
                values.push_back(value);
            } catch (const std::exception& e) {
                // Ignore non-numeric lines
            }
        }
        return values;
    }

    static int loadFanStartTemp(const std::string& configFile) {
        return loadIntParam(configFile, "fan_start_temp:");
    }

    static int loadFanStopTemp(const std::string& configFile) {
        return loadIntParam(configFile, "fan_stop_temp:");
    }

    static int loadPwmMin(const std::string& configFile) {
        return loadIntParam(configFile, "pwm_min:");
    }

    static int loadPwmMax(const std::string& configFile) {
        return loadIntParam(configFile, "pwm_max:");
    }

    static int loadTempMin(const std::string& configFile) {
        return loadIntParam(configFile, "temp_min:");
    }

    static int loadTempMax(const std::string& configFile) {
        return loadIntParam(configFile, "temp_max:");
    }

    // 加载模式-specific PWM 值
    static int loadModeQuietPwmMin(const std::string& configFile) {
        return loadIntParam(configFile, "mode_quiet_pwm_min:");
    }

    static int loadModeQuietPwmMax(const std::string& configFile) {
        return loadIntParam(configFile, "mode_quiet_pwm_max:");
    }

    static int loadModeNormalPwmMin(const std::string& configFile) {
        return loadIntParam(configFile, "mode_normal_pwm_min:");
    }

    static int loadModeNormalPwmMax(const std::string& configFile) {
        return loadIntParam(configFile, "mode_normal_pwm_max:");
    }

    static int loadModePerformancePwmMin(const std::string& configFile) {
        return loadIntParam(configFile, "mode_performance_pwm_min:");
    }

    static int loadModePerformancePwmMax(const std::string& configFile) {
        return loadIntParam(configFile, "mode_performance_pwm_max:");
    }

    // 新增：加载是否启用温度停滞强制最大 PWM（1=启用, 0=禁用）
    static int loadForceOnStagnation(const std::string& configFile) {
        return loadIntParam(configFile, "force_on_stagnation:");
    }

    // 强制最大 PWM 的最长持续时间（秒）
    static int loadForceDurationSec(const std::string& configFile) {
        return loadIntParam(configFile, "force_duration_sec:");
    }

    // 检测温度是否停滞的间隔（秒）
    static int loadStagnationCheckIntervalSec(const std::string& configFile) {
        return loadIntParam(configFile, "stagnation_check_interval_sec:");
    }

    // 停滞判断使用的温度下降阈值（小于该值视为未明显下降），浮点
    static double loadStagnationTempDropThreshold(const std::string& configFile) {
        return loadFloatParam(configFile, "stagnation_temp_drop_threshold:");
    }

    // 强制结束后再次触发强制的冷却时间（秒），用于避免频繁启停
    static int loadStagnationForceCooldownSec(const std::string& configFile) {
        return loadIntParam(configFile, "stagnation_force_cooldown_sec:");
    }

    // 日志开关（0=默认不打印, 1=打印）
    static int loadEnableLogging(const std::string& configFile) {
        return loadIntParam(configFile, "enable_logging:");
    }

    // 是否发布风扇速度到 ZMQ 以及发送间隔（秒）
    static int loadPublishFanSpeed(const std::string& configFile) {
        return loadIntParam(configFile, "publish_fan_speed:");
    }

    static int loadPublishFanSpeedInterval(const std::string& configFile) {
        return loadIntParam(configFile, "publish_fan_speed_interval_sec:");
    }

    // 冷启动时长（秒），在服务启动后以最大 PWM 保持运行该时长后进入正常调速
    static int loadColdStartDuration(const std::string& configFile) {
        return loadIntParam(configFile, "cold_start_duration_sec:");
    }

    static double loadFloatParam(const std::string& configFile, const std::string& param) {
        std::ifstream file(configFile);
        if (!file.is_open()) {
            std::cerr << "Failed to open config file: " << configFile << std::endl;
            return 0.0;
        }
        std::string line;
        while (std::getline(file, line)) {
            size_t first = line.find_first_not_of(" \t\r\n");
            if (first == std::string::npos) continue;
            if (line[first] == '#') continue;
            if (line.find(param) != std::string::npos) {
                size_t pos = line.find(":");
                if (pos != std::string::npos) {
                    std::string valueStr = line.substr(pos + 1);
                    try {
                        return std::stod(valueStr);
                    } catch (const std::exception& e) {
                        std::cerr << "Invalid " << param << " value in config: " << valueStr << std::endl;
                    }
                }
            }
        }
        return 0.0;
    }

    // 检查参数是否在配置文件中存在（用于区分显式设置为0和未设置）
    static bool paramExists(const std::string& configFile, const std::string& param) {
        std::ifstream file(configFile);
        if (!file.is_open()) return false;
        std::string line;
        while (std::getline(file, line)) {
            size_t first = line.find_first_not_of(" \t\r\n");
            if (first == std::string::npos) continue;
            if (line[first] == '#') continue;
            if (line.find(param) != std::string::npos) return true;
        }
        return false;
    }

private:
    static int loadIntParam(const std::string& configFile, const std::string& param) {
        std::ifstream file(configFile);
        if (!file.is_open()) {
            std::cerr << "Failed to open config file: " << configFile << std::endl;
            return 0;
        }
        std::string line;
        while (std::getline(file, line)) {
            // 忽略注释行（以 # 开头）和空行
            size_t first = line.find_first_not_of(" \t\r\n");
            if (first == std::string::npos) continue;
            if (line[first] == '#') continue;
            if (line.find(param) != std::string::npos) {
                size_t pos = line.find(":");
                if (pos != std::string::npos) {
                    std::string valueStr = line.substr(pos + 1);
                    try {
                        return std::stoi(valueStr);
                    } catch (const std::exception& e) {
                        std::cerr << "Invalid " << param << " value in config: " << valueStr << std::endl;
                    }
                }
            }
        }
        return 0;
    }
};

std::string getCurrentTime() {
    std::time_t now = std::time(nullptr);
    char buf[100];
    std::strftime(buf, sizeof(buf), "%Y%m%d_%H%M%S", std::localtime(&now));
    return buf;
}

// PWM 常量（PCA9685 12-bit）
constexpr uint16_t PWM_FULL_BRIGHTNESS = 0;   // 0 -> 最大亮度（对 RGB LED 取反逻辑）
constexpr uint16_t PWM_FULL_OFF = 4095;       // 4095 -> 关闭


float getCpuTemperature() {
    std::ifstream tempFile("/sys/class/thermal/thermal_zone0/temp");
    if (!tempFile.is_open()) {
        std::cerr << "Failed to read CPU temperature" << std::endl;
        return 0.0f;
    }
    int tempMilli;
    tempFile >> tempMilli;
    return tempMilli / 1000.0f;
}

uint16_t calculatePwmFromTemp(float temp, int fanStartTemp, int fanStopTemp, int pwmMin, int pwmMax, int tempMin, int tempMax) {
    if (temp < fanStopTemp) {
        return 0;  // 温度低于停止温度，风扇停止
    } else if (temp < fanStartTemp) {
        return pwmMin;  // 温度在停止和启动之间，保持最小转速
    } else if (temp >= tempMax) {
        return pwmMax;  // 温度超过最大值，全速转动
    } else {
        // 线性调节：从tempMin到tempMax，PWM从pwmMin到pwmMax
        float ratio = (temp - tempMin) / (float)(tempMax - tempMin);
        int pwm = pwmMin + (pwmMax - pwmMin) * ratio;
        return std::min(pwmMax, std::max(pwmMin, pwm));
    }
}

// 全局变量用于信号处理
std::atomic<bool> running(true);
std::atomic<bool> reloadConfig(false);

void signalHandler(int signum) {
    if (signum == SIGTERM || signum == SIGINT) {
        std::cout << "Received signal " << signum << ", shutting down..." << std::endl;
        running = false;
    } else if (signum == SIGHUP) {
        std::cout << "Received SIGHUP, reloading configuration..." << std::endl;
        reloadConfig = true;
    }
}

// 简单的 streambuf，向两个目标同时写入（文件 + 控制台）
class TeeBuf : public std::streambuf {
public:
    TeeBuf(std::streambuf* a, std::streambuf* b) : a(a), b(b) {}
protected:
    int overflow(int c) override {
        if (c == EOF) return !EOF;
        if (a->sputc(c) == EOF) return EOF;
        if (b->sputc(c) == EOF) return EOF;
        return c;
    }
    std::streamsize xsputn(const char* s, std::streamsize n) override {
        std::streamsize r1 = a->sputn(s, n);
        std::streamsize r2 = b->sputn(s, n);
        return (r1 < r2) ? r1 : r2;
    }
private:
    std::streambuf* a;
    std::streambuf* b;
};

int runFanService(int argc, char** argv) {
    // 支持命令行参数：
    // -c <path> 或 --config <path> : 指定配置文件路径（默认 ../../config/fan_config.txt）
    // --console : 输出到控制台
    // -f <path> 或 --log-file <path> : 启用文件日志（只有在传入此参数时才写文件）
    // --bus-endpoint <endpoint> : 指定 ZMQ 总线端点（默认 ipc:///tmp/doly_fan_zmq.sock）
    bool logToConsole = false;
    bool fileLogging = false;
    std::string logFileArg;
    std::string configFileArg;
    std::string busEndpoint = "ipc:///tmp/doly_fan_zmq.sock";
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--console") {
            logToConsole = true;
        } else if (a == "-f" || a == "--log-file") {
            if (i + 1 < argc) {
                fileLogging = true;
                logFileArg = argv[++i];
            } else {
                std::cerr << "Missing argument for " << a << std::endl;
            }
        } else if (a == "-c" || a == "--config") {
            if (i + 1 < argc) {
                configFileArg = argv[++i];
            } else {
                std::cerr << "Missing argument for " << a << std::endl;
            }
        } else if (a == "--bus-endpoint") {
            if (i + 1 < argc) {
                busEndpoint = argv[++i];
            } else {
                std::cerr << "Missing argument for --bus-endpoint" << std::endl;
            }
        }
    }

    // 设置信号处理
    signal(SIGTERM, signalHandler);
    signal(SIGINT, signalHandler);
    signal(SIGHUP, signalHandler);

    // 默认配置文件可以通过 -c / --config 指定
    std::string configFile = configFileArg.empty() ? std::string("/home/pi/dolydev/config/fan_config.txt") : configFileArg;

    // 默认不写文件，仅在 --log-file/-f 提供时写入日志文件
    std::string logFile;
    if (fileLogging) {
        logFile = logFileArg.empty() ? (std::string("../../logs/fan_service_") + getCurrentTime() + ".log") : logFileArg;
    }

    std::cout << "Starting Fan Temperature Control Service..." << std::endl;
    std::cout << "Config file: " << configFile << std::endl;
    if (fileLogging) std::cout << "Log file: " << logFile << std::endl;
    std::cout << "ZMQ bus endpoint: " << busEndpoint << std::endl;

    std::ofstream ofs;
    if (fileLogging) {
        ofs.open(logFile, std::ios::app);
        if (!ofs.is_open()) {
            std::cerr << "Failed to open log file: " << logFile << std::endl;
            // fallback to console only
            fileLogging = false;
            logToConsole = true;
        }
    }

    // 根据命令行选项决定日志目标：
    // - 如果启用文件日志 (fileLogging)，且同时要求控制台 (logToConsole)，则 tee 到两个目标
    // - 如果只有文件日志，则写文件
    // - 否则默认输出到控制台
    std::unique_ptr<TeeBuf> teeBuf;
    std::unique_ptr<std::ostream> teeStream;
    std::ostream* log = &std::cout;
    if (fileLogging) {
        if (logToConsole) {
            teeBuf.reset(new TeeBuf(ofs.rdbuf(), std::cout.rdbuf()));
            teeStream.reset(new std::ostream(teeBuf.get()));
            log = teeStream.get();
        } else {
            log = &ofs;
        }
    } else {
        // no file logging: ensure we at least log to console
        log = &std::cout;
    }

    // Start ZMQ threads (command subscriber + status publisher)
    std::thread zmq_cmd_thread(commandThread, busEndpoint, log);
    std::thread zmq_status_thread(statusThread, busEndpoint, 5000 /*ms*/, log);

    (*log) << "=== Fan Temperature Control Service Started ===" << std::endl;
    (*log) << "Config file: " << configFile << std::endl;
    (*log) << "Started at: " << getCurrentTime() << std::endl;

    // 加载初始配置
    int fanStartTemp = Config::loadFanStartTemp(configFile);
    int fanStopTemp = Config::loadFanStopTemp(configFile);
    int pwmMin = Config::loadPwmMin(configFile);
    int pwmMax = Config::loadPwmMax(configFile);
    int tempMin = Config::loadTempMin(configFile);
    int tempMax = Config::loadTempMax(configFile);
    // 新增：停滞检测/强制开最大 PWM 的可配置项
    int cfg_force_on_stagnation = Config::loadForceOnStagnation(configFile); // 1 enable, 0 disable
    int cfg_force_duration_sec = Config::loadForceDurationSec(configFile);
    int cfg_stagnation_check_interval_sec = Config::loadStagnationCheckIntervalSec(configFile);
    double cfg_stagnation_temp_drop_threshold = Config::loadStagnationTempDropThreshold(configFile);
    int cfg_stagnation_force_cooldown_sec = Config::loadStagnationForceCooldownSec(configFile);

    // Respect explicit setting: if param exists, use it; otherwise default to enabled
    if (!Config::paramExists(configFile, "force_on_stagnation:") ) {
        cfg_force_on_stagnation = 1;
    }
    if (cfg_force_duration_sec <= 0) cfg_force_duration_sec = 30; // default 30s
    if (cfg_stagnation_check_interval_sec <= 0) cfg_stagnation_check_interval_sec = 10; // default 10s
    if (cfg_stagnation_temp_drop_threshold <= 0.0) cfg_stagnation_temp_drop_threshold = 0.2; // default 0.2°C
    if (cfg_stagnation_force_cooldown_sec <= 0) cfg_stagnation_force_cooldown_sec = 60; // default 60s cooldown

    (*log) << "Stagnation config - enabled: " << cfg_force_on_stagnation
        << ", check_interval_s: " << cfg_stagnation_check_interval_sec
        << ", temp_drop_threshold_C: " << cfg_stagnation_temp_drop_threshold
        << ", force_duration_s: " << cfg_force_duration_sec
        << ", cooldown_s: " << cfg_stagnation_force_cooldown_sec << std::endl;

    // 加载模式-specific PWM 配置
    {
        std::lock_guard<std::mutex> lock(g_mode_pwm_mutex);
        g_mode_pwm_config["quiet"].pwm_min = Config::loadModeQuietPwmMin(configFile);
        g_mode_pwm_config["quiet"].pwm_max = Config::loadModeQuietPwmMax(configFile);
        g_mode_pwm_config["normal"].pwm_min = Config::loadModeNormalPwmMin(configFile);
        g_mode_pwm_config["normal"].pwm_max = Config::loadModeNormalPwmMax(configFile);
        g_mode_pwm_config["performance"].pwm_min = Config::loadModePerformancePwmMin(configFile);
        g_mode_pwm_config["performance"].pwm_max = Config::loadModePerformancePwmMax(configFile);
    }

    (*log) << "Initial config - Start: " << fanStartTemp << "°C, Stop: " << fanStopTemp << "°C" << std::endl;
    (*log) << "PWM range: " << pwmMin << "-" << pwmMax << ", Temp range: " << tempMin << "-" << tempMax << "°C" << std::endl;
    (*log) << "Mode PWM - quiet: " << g_mode_pwm_config["quiet"].pwm_min << "-" << g_mode_pwm_config["quiet"].pwm_max
           << ", normal: " << g_mode_pwm_config["normal"].pwm_min << "-" << g_mode_pwm_config["normal"].pwm_max
           << ", performance: " << g_mode_pwm_config["performance"].pwm_min << "-" << g_mode_pwm_config["performance"].pwm_max << std::endl;

    // 初始化PWM
    // Initialize FanControl subsystem (manual mode)
    if (FanControl::init(false) < 0) {
        std::cerr << "FanControl init failed" << std::endl;
        (*log) << "FanControl init failed" << std::endl;
        return 1;
    }

    (*log) << "FanControl initialized successfully" << std::endl;
    std::cout << "Service initialized successfully, entering main loop..." << std::endl;

    // 新增：日志开关与风扇速度发布配置
    int cfg_enable_logging = Config::loadEnableLogging(configFile);
    int cfg_publish_fan_speed = Config::loadPublishFanSpeed(configFile);
    int cfg_publish_fan_speed_interval = Config::loadPublishFanSpeedInterval(configFile);

    // 新增：冷启动配置（默认 5 秒）
    int cfg_cold_start_duration_sec = Config::loadColdStartDuration(configFile);
    if (!Config::paramExists(configFile, "cold_start_duration_sec:")) {
        cfg_cold_start_duration_sec = 5;
    }
    if (cfg_cold_start_duration_sec <= 0) cfg_cold_start_duration_sec = 5;

    // 默认：不打印日志（除非命令行或配置打开）
    NullBuf nullbuf;
    std::ostream nullStream(&nullbuf);
    if (cfg_enable_logging == 0 && !logToConsole && !fileLogging) {
        // disable logs by pointing to null stream
        log = &nullStream;
    }

    // 启动 fan speed 发布线程
    g_publish_fan_speed_enabled.store(cfg_publish_fan_speed);
    if (cfg_publish_fan_speed_interval > 0) g_publish_fan_speed_interval_sec.store(cfg_publish_fan_speed_interval);
    std::thread fan_speed_thread(fanSpeedPublishThread, busEndpoint, log);

    // 主循环
    int iteration = 0;
    float lastTemp = 0.0f;
    auto lastCheckTime = std::chrono::steady_clock::now();
    bool forceMaxPwm = false;
    auto forceMaxStartTime = std::chrono::steady_clock::now();
    auto lastForceEndTime = std::chrono::steady_clock::time_point::min();

    // 状态机变量：系统化冷启动处理
    bool prevPwmWasZero = true; 
    bool inColdStart = false;
    auto coldStartEndTime = std::chrono::steady_clock::now();

    while (running) {
        // 检查是否需要重载配置
        if (reloadConfig) {
            // reload logging / publish settings
            int cfg_enable_logging_new = Config::loadEnableLogging(configFile);
            int cfg_publish_fan_speed_new = Config::loadPublishFanSpeed(configFile);
            int cfg_publish_fan_speed_interval_new = Config::loadPublishFanSpeedInterval(configFile);

            // update publish settings
            g_publish_fan_speed_enabled.store(cfg_publish_fan_speed_new);
            if (cfg_publish_fan_speed_interval_new > 0) g_publish_fan_speed_interval_sec.store(cfg_publish_fan_speed_interval_new);

            // update logging: if disabled, switch to null stream
            if (cfg_enable_logging_new == 0 && !logToConsole && !fileLogging) {
                log = &nullStream; // silent
            } else {
                // if fileLogging or console requested, leave log as is (teeStream or ofs or cout)
                if (fileLogging) {
                    if (logToConsole) log = teeStream.get(); else log = &ofs;
                } else {
                    log = &std::cout;
                }
            }

         // reload stagnation config
         cfg_force_on_stagnation = Config::loadForceOnStagnation(configFile);
         cfg_force_duration_sec = Config::loadForceDurationSec(configFile);
         cfg_stagnation_check_interval_sec = Config::loadStagnationCheckIntervalSec(configFile);
         cfg_stagnation_temp_drop_threshold = Config::loadStagnationTempDropThreshold(configFile);
         cfg_stagnation_force_cooldown_sec = Config::loadStagnationForceCooldownSec(configFile);

         if (cfg_force_on_stagnation == 0) cfg_force_on_stagnation = 1;
         if (cfg_force_duration_sec <= 0) cfg_force_duration_sec = 30;
         if (cfg_stagnation_check_interval_sec <= 0) cfg_stagnation_check_interval_sec = 10;
         if (cfg_stagnation_temp_drop_threshold <= 0.0) cfg_stagnation_temp_drop_threshold = 0.2;
         if (cfg_stagnation_force_cooldown_sec <= 0) cfg_stagnation_force_cooldown_sec = 60;

         (*log) << "Stagnation config reloaded - enabled: " << cfg_force_on_stagnation
             << ", check_interval_s: " << cfg_stagnation_check_interval_sec
             << ", temp_drop_threshold_C: " << cfg_stagnation_temp_drop_threshold
             << ", force_duration_s: " << cfg_force_duration_sec
             << ", cooldown_s: " << cfg_stagnation_force_cooldown_sec << std::endl;
            fanStartTemp = Config::loadFanStartTemp(configFile);
            fanStopTemp = Config::loadFanStopTemp(configFile);
            pwmMin = Config::loadPwmMin(configFile);
            pwmMax = Config::loadPwmMax(configFile);
            tempMin = Config::loadTempMin(configFile);
            tempMax = Config::loadTempMax(configFile);

            // 重载模式-specific PWM 配置
            {
                std::lock_guard<std::mutex> lock(g_mode_pwm_mutex);
                g_mode_pwm_config["quiet"].pwm_min = Config::loadModeQuietPwmMin(configFile);
                g_mode_pwm_config["quiet"].pwm_max = Config::loadModeQuietPwmMax(configFile);
                g_mode_pwm_config["normal"].pwm_min = Config::loadModeNormalPwmMin(configFile);
                g_mode_pwm_config["normal"].pwm_max = Config::loadModeNormalPwmMax(configFile);
                g_mode_pwm_config["performance"].pwm_min = Config::loadModePerformancePwmMin(configFile);
                g_mode_pwm_config["performance"].pwm_max = Config::loadModePerformancePwmMax(configFile);
            }

            (*log) << "Configuration reloaded at " << getCurrentTime() << std::endl;
            (*log) << "New config - Start: " << fanStartTemp << "°C, Stop: " << fanStopTemp << "°C" << std::endl;
            (*log) << "Mode PWM reloaded - quiet: " << g_mode_pwm_config["quiet"].pwm_min << "-" << g_mode_pwm_config["quiet"].pwm_max
                   << ", normal: " << g_mode_pwm_config["normal"].pwm_min << "-" << g_mode_pwm_config["normal"].pwm_max
                   << ", performance: " << g_mode_pwm_config["performance"].pwm_min << "-" << g_mode_pwm_config["performance"].pwm_max << std::endl;
            reloadConfig = false;
        }

        float temp = getCpuTemperature();
        uint16_t pwm;

        // 优先级顺序（推荐）：
        // 1. 全局禁用（disable） — 紧急停止
        // 2. 静音禁止（inhibit） — 由音频前端自动触发（用于防止唤醒词检测时风扇噪音干扰）
        // 3. 温度强制max（>65°C） — 硬件保护（仅在 inhibit 不活跃时触发）
        // 4. 持久模式 — 用户主动选择，保持不变
        // 5. 手动PWM — 动态控制
        // 6. 临时模式 — 限时切换，过期后回到自动
        // 7. 自动温控 — 默认行为
        
        if (!g_fan_enabled.load()) {
            // 全局禁用：紧急停止，风扇关闭
            pwm = 0;
        } else if (anyInhibitActive()) {
            // 静音禁止：优先级高于温度保护，确保唤醒词检测时风扇静音
            pwm = 0;
        } else if (forceMaxPwm) {
            // 温度强制max：只有在没有 inhibit 时才触发
            pwm = pwmMax;
        } else if (isModeActive()) {
            // 模式有效（持久或未过期的临时模式）
            std::string activeMode = getCurrentMode();
            auto pwm_config = getModePwmRange(activeMode);
            
            uint16_t raw = calculatePwmFromTemp(temp, fanStartTemp, fanStopTemp, pwmMin, pwmMax, tempMin, tempMax);
            int mode_pwm_min = pwm_config.pwm_min;
            int mode_pwm_max = pwm_config.pwm_max;
            
            // 在模式的 PWM 范围内重新计算
            if (raw == 0) {
                pwm = 0;
            } else {
                // 根据原始 PWM 的百分比映射到模式范围内
                double ratio = static_cast<double>(raw - pwmMin) / (pwmMax - pwmMin);
                int pwm_result = mode_pwm_min + static_cast<int>((mode_pwm_max - mode_pwm_min) * ratio);
                pwm = static_cast<uint16_t>(std::clamp(pwm_result, mode_pwm_min, mode_pwm_max));
            }
        } else if (manualOverrideActive()) {
            int manual = g_manual_pwm.load();
            pwm = static_cast<uint16_t>(std::clamp(manual, 0, 4095));
        } else {
            // normal temperature-based control
            pwm = calculatePwmFromTemp(temp, fanStartTemp, fanStopTemp, pwmMin, pwmMax, tempMin, tempMax);
        }

        // 系统化冷启动逻辑：捕获从 0（停转）到非 0（启动）的跳变
        if (pwm > 0) {
            if (prevPwmWasZero) {
                inColdStart = true;
                coldStartEndTime = std::chrono::steady_clock::now() + std::chrono::seconds(cfg_cold_start_duration_sec);
                (*log) << "[ColdStart] Fan start detected. Forcing PWM 4095 for " << cfg_cold_start_duration_sec << "s..." << std::endl;
            }
            
            if (inColdStart) {
                if (std::chrono::steady_clock::now() < coldStartEndTime) {
                    pwm = 4095; // 强制最大值以克服静摩擦和启动电流
                } else {
                    inColdStart = false;
                    (*log) << "[ColdStart] Cold-start completed, transitioning to target PWM: " << pwm << std::endl;
                }
            }
            prevPwmWasZero = false;
        } else {
            if (!prevPwmWasZero) {
                (*log) << "[ColdStart] Fan stopped (PWM=0)." << std::endl;
            }
            prevPwmWasZero = true;
            inColdStart = false; // 如果在冷启动期间被关闭（如被静音），重置标志
        }

        // 检查温度趋势（按配置间隔）
        auto currentTime = std::chrono::steady_clock::now();
        auto timeSinceLastCheck = std::chrono::duration_cast<std::chrono::seconds>(currentTime - lastCheckTime).count();

        if (iteration > 0 && timeSinceLastCheck >= cfg_stagnation_check_interval_sec) {
            // 判断是否在强制冷却期内（避免频繁触发）
            bool inCooldown = (lastForceEndTime != std::chrono::steady_clock::time_point::min()) &&
                              (std::chrono::duration_cast<std::chrono::seconds>(currentTime - lastForceEndTime).count() < cfg_stagnation_force_cooldown_sec);

            if (cfg_force_on_stagnation && !forceMaxPwm && !inCooldown) {
                // 如果温度没有明显下降（小于阈值），则触发强制
                double tempDiff = static_cast<double>(temp) - static_cast<double>(lastTemp);
                if (tempDiff >= -cfg_stagnation_temp_drop_threshold) {
                    // temp increased or decreased less than threshold -> treat as stagnation
                    forceMaxPwm = true;
                    forceMaxStartTime = currentTime;
                    (*log) << "Temperature stagnation detected (" << lastTemp << "\u00B0C -> " << temp << "\u00B0C), forcing max PWM for " << cfg_force_duration_sec << "s at " << getCurrentTime() << std::endl;
                }
            } else if (forceMaxPwm) {
                // 如果已经处于强制状态，检查是否超过持续时间或已经出现下降
                auto forcedSec = std::chrono::duration_cast<std::chrono::seconds>(currentTime - forceMaxStartTime).count();
                if (forcedSec >= cfg_force_duration_sec || temp < lastTemp - cfg_stagnation_temp_drop_threshold) {
                    forceMaxPwm = false;
                    lastForceEndTime = currentTime;
                    (*log) << "Ending forced max PWM after " << forcedSec << "s at " << getCurrentTime() << " (temp: " << lastTemp << " -> " << temp << ")" << std::endl;
                }
            }

            lastCheckTime = currentTime;
            lastTemp = temp;
        } else if (iteration == 0) {
            lastTemp = temp;
            lastCheckTime = currentTime;
        }

        // 记录状态（每30秒或有变化时）
        if (iteration % 30 == 0 || pwm != calculatePwmFromTemp(lastTemp, fanStartTemp, fanStopTemp, pwmMin, pwmMax, tempMin, tempMax)) {
            (*log) << "Status - Temp: " << temp << "\u00B0C, PWM: " << pwm;
            if (forceMaxPwm) {
                (*log) << " (FORCED MAX)";
            }
            (*log) << " at " << getCurrentTime() << std::endl;
        }

        // 设置PWM
        // write speed through FanControl API; convert 0-4095 to percentage
        int pct = 0;
        if (pwm > 0) {
            pct = static_cast<int>(std::round(pwm * 100.0 / 4095.0));
        }
        FanControl::setFanSpeed(pct);
        // 保存当前 pwm 以便发布线程读取
        g_current_pwm.store(static_cast<int>(pwm));
        // FanControl::setFanSpeed doesn't return error code, so log rarely needed

        iteration++;
        std::this_thread::sleep_for(std::chrono::seconds(1)); // 1秒检查一次
    }

    // stop ZMQ threads
    if (zmq_cmd_thread.joinable()) zmq_cmd_thread.join();
    if (zmq_status_thread.joinable()) zmq_status_thread.join();

    // 关闭服务
    FanControl::setFanSpeed(0); // 停止风扇
    FanControl::dispose();
    (*log) << "Service stopped at " << getCurrentTime() << std::endl;
    // flush file (let destructor close it)
    ofs.flush();

    std::cout << "Fan Temperature Control Service stopped." << std::endl;
    return 0;
}
