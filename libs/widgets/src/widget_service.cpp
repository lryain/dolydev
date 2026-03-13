/**
 * @file widget_service.cpp
 * @brief Doly Widget 独立服务 - 时钟和定时器 Widget 管理
 * 
 * 功能:
 * - 作为独立进程运行，管理时钟和定时器 Widget
 * - 通过 ZMQ 订阅 cmd.widget.* 命令
 * - 通过 ZMQ 发布 event.widget.* 事件和 status.widget.* 状态
 * - 管理 LCD 互斥显示（与 modules/eyeEngine 协调）
 * - 支持时钟显示、整点报时、倒计时/正计时器
 * 
 * 用法:
 *   ./widget_service [--config <config.json>] [--no-lcd]
 */

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

// POSIX
#include <fcntl.h>
#include <unistd.h>
#include <sys/file.h>

#include <zmq.hpp>
#include <nlohmann/json.hpp>

// EyeEngine 复用的 widget 组件
#include "doly/eye_engine/lcd_transport.h"
#include "doly/eye_engine/frame_swap_chain.h"
#include "doly/eye_engine/logging.h"
#include "doly/eye_engine/widgets/widget_manager.h"
#include "doly/eye_engine/widgets/clock_widget.h"
#include "doly/eye_engine/widgets/timer_widget.h"

using json = nlohmann::json;
using namespace doly::eye_engine;

// ======================== 全局变量 ========================
static std::atomic<bool> g_running{true};
static std::string g_config_path;

// ======================== 信号处理 ========================
static void signalHandler(int sig) {
    std::cout << "[WidgetService] 收到信号 " << sig << "，准备退出..." << std::endl;
    g_running = false;
}

// ======================== LCD 互斥锁 ========================
class LcdMutex {
public:
    explicit LcdMutex(const std::string& lock_path = "/tmp/doly_lcd.lock")
        : lock_path_(lock_path) {}

    bool acquire() {
        fd_ = open(lock_path_.c_str(), O_CREAT | O_RDWR, 0666);
        if (fd_ < 0) {
            std::cerr << "[LcdMutex] 无法打开锁文件: " << lock_path_ << std::endl;
            return false;
        }
        struct flock fl{};
        fl.l_type = F_WRLCK;
        fl.l_whence = SEEK_SET;
        fl.l_start = 0;
        fl.l_len = 0;
        if (fcntl(fd_, F_SETLK, &fl) == -1) {
            std::cerr << "[LcdMutex] LCD 锁被其他进程持有" << std::endl;
            close(fd_);
            fd_ = -1;
            return false;
        }
        locked_ = true;
        std::cout << "[LcdMutex] LCD 锁已获取" << std::endl;
        return true;
    }

    void release() {
        if (fd_ >= 0 && locked_) {
            struct flock fl{};
            fl.l_type = F_UNLCK;
            fl.l_whence = SEEK_SET;
            fl.l_start = 0;
            fl.l_len = 0;
            fcntl(fd_, F_SETLK, &fl);
            close(fd_);
            fd_ = -1;
            locked_ = false;
            std::cout << "[LcdMutex] LCD 锁已释放" << std::endl;
        }
    }

    bool isLocked() const { return locked_; }

    ~LcdMutex() { release(); }

private:
    std::string lock_path_;
    int fd_ = -1;
    bool locked_ = false;
};

// ======================== Widget Service 配置 ========================
struct WidgetServiceConfig {
    // ZMQ 端点
    std::string sub_endpoint = "ipc:///tmp/doly_bus.sock";
    std::string pub_endpoint = "ipc:///tmp/doly_widget_pub.sock";
    
    // LCD 锁
    std::string lcd_lock_path = "/tmp/doly_lcd.lock";
    
    // 渲染
    double render_fps = 10.0;
    bool no_lcd = false;

    // Widget 配置文件路径
    std::string widgets_config_path;
    
    static WidgetServiceConfig fromJson(const json& j) {
        WidgetServiceConfig cfg;
        if (j.contains("service")) {
            const auto& svc = j["service"];
            cfg.sub_endpoint = svc.value("sub_endpoint", cfg.sub_endpoint);
            cfg.pub_endpoint = svc.value("pub_endpoint", cfg.pub_endpoint);
            cfg.lcd_lock_path = svc.value("lcd_lock_path", cfg.lcd_lock_path);
            cfg.render_fps = svc.value("render_fps", cfg.render_fps);
        }
        return cfg;
    }
};

// ======================== Widget Service 主类 ========================
class WidgetService {
public:
    WidgetService() = default;
    ~WidgetService() { stop(); }

    bool initialize(const WidgetServiceConfig& config) {
        config_ = config;
        
        std::cout << "[WidgetService] 初始化..." << std::endl;
        std::cout << "[WidgetService]   SUB endpoint: " << config_.sub_endpoint << std::endl;
        std::cout << "[WidgetService]   PUB endpoint: " << config_.pub_endpoint << std::endl;
        std::cout << "[WidgetService]   LCD lock: " << config_.lcd_lock_path << std::endl;
        std::cout << "[WidgetService]   渲染FPS: " << config_.render_fps << std::endl;
        std::cout << "[WidgetService]   无LCD模式: " << (config_.no_lcd ? "是" : "否") << std::endl;

        // 1. 初始化 LCD (如果不是无LCD模式)
        if (!config_.no_lcd) {
            transport_.reset(createLcdControlTransport());
            if (!transport_) {
                std::cerr << "[WidgetService] 创建 LCD transport 失败" << std::endl;
                return false;
            }

            std::vector<DisplayConfig> lcd_configs;
            lcd_configs.push_back(DisplayConfig{LcdLeft});
            lcd_configs.push_back(DisplayConfig{LcdRight});
            
            // 暂不初始化LCD，等到需要显示widget时再初始化
            // 这样可以避免与 modules/eyeEngine 冲突
            std::cout << "[WidgetService] LCD transport 已创建（延迟初始化）" << std::endl;
        }

        // 2. 初始化 FrameBuffer (RGB565 格式渲染)
        // ClockWidget 使用 RGB565 (2字节/像素) 进行渲染
        // 缓冲区大小必须与 ClockWidget 分配大小匹配 (240 * 240 * 2 = 115200 字节)
        constexpr size_t kBufferSize = 240 * 240 * 2; // RGB565: 115200 字节
        std::cout << "[WidgetService] 分配 FrameBuffer: " << kBufferSize << " 字节 (RGB565)" << std::endl;
        frame_left_.allocate(kBufferSize);
        frame_right_.allocate(kBufferSize);
        frame_left_.clear();
        frame_right_.clear();

        // 3. 创建 Widget Manager 并注册 widgets
        auto clock = std::make_unique<ClockWidget>();
        auto timer = std::make_unique<TimerWidget>();
        
        // 设置定时器事件回调
        timer->setEventEmitter([this](const std::string& event_type, const json& data) {
            if (event_type == kTimerAutoShowTopic) {
                handleTimerAutoShow(data);
            }
            publishEvent("event.widget.timer." + event_type, data);
        });
        
        widget_manager_.registerWidget("clock", std::move(clock));
        widget_manager_.registerWidget("timer", std::move(timer));
        
        std::cout << "[WidgetService] Widget Manager 已初始化 (clock, timer)" << std::endl;

        // 4. 加载 widget 配置
        loadWidgetConfig();

        // 5. 初始化 ZMQ
        if (!initZmq()) {
            std::cerr << "[WidgetService] ZMQ 初始化失败" << std::endl;
            return false;
        }

        // 6. 初始化 LCD 互斥
        lcd_mutex_ = std::make_unique<LcdMutex>(config_.lcd_lock_path);

        initialized_ = true;
        std::cout << "[WidgetService] 初始化完成" << std::endl;
        return true;
    }

    bool start() {
        if (!initialized_) {
            std::cerr << "[WidgetService] 未初始化" << std::endl;
            return false;
        }

        running_ = true;

        // 启动 ZMQ 命令订阅线程
        sub_thread_ = std::thread([this]() { subscriberLoop(); });

        // 启动渲染循环线程
        render_thread_ = std::thread([this]() { renderLoop(); });

        std::cout << "[WidgetService] 服务已启动" << std::endl;
        return true;
    }

    void stop() {
        if (!running_) return;
        running_ = false;

        if (sub_thread_.joinable()) sub_thread_.join();
        if (render_thread_.joinable()) render_thread_.join();

        // 释放 LCD
        releaseLcd();

        std::cout << "[WidgetService] 服务已停止" << std::endl;
    }

    bool isRunning() const { return running_; }

private:
    // ==================== ZMQ 初始化 ====================
    bool initZmq() {
        try {
            zmq_ctx_ = std::make_unique<zmq::context_t>(1);

            // SUB socket - 订阅 widget 命令
            zmq_sub_ = std::make_unique<zmq::socket_t>(*zmq_ctx_, ZMQ_SUB);
            zmq_sub_->connect(config_.sub_endpoint);
            
            // 订阅 widget 相关话题
            const char* topic1 = "cmd.widget.";
            zmq_sub_->setsockopt(ZMQ_SUBSCRIBE, topic1, strlen(topic1));
            const char* topic2 = "cmd.eye.widget.";
            zmq_sub_->setsockopt(ZMQ_SUBSCRIBE, topic2, strlen(topic2));
            int rcvtimeo = 100; // 100ms 超时
            zmq_sub_->setsockopt(ZMQ_RCVTIMEO, &rcvtimeo, sizeof(rcvtimeo));
            
            std::cout << "[WidgetService] ZMQ SUB 已连接: " << config_.sub_endpoint << std::endl;

            // PUB socket - 发布事件
            zmq_pub_ = std::make_unique<zmq::socket_t>(*zmq_ctx_, ZMQ_PUB);
            zmq_pub_->bind(config_.pub_endpoint);
            
            // 等待 slow joiner
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            
            std::cout << "[WidgetService] ZMQ PUB 已绑定: " << config_.pub_endpoint << std::endl;
            
            return true;
        } catch (const zmq::error_t& e) {
            std::cerr << "[WidgetService] ZMQ 错误: " << e.what() << std::endl;
            return false;
        }
    }

    // ==================== ZMQ 事件发布 ====================
    void publishEvent(const std::string& topic, const json& data) {
        std::lock_guard<std::mutex> lock(pub_mutex_);
        try {
            if (!zmq_pub_) return;
            std::string payload = data.dump();
            zmq_pub_->send(zmq::buffer(topic), zmq::send_flags::sndmore);
            zmq_pub_->send(zmq::buffer(payload), zmq::send_flags::none);
            // std::cout << "[WidgetService] 发布事件: " << topic << " => " << payload << std::endl;
        } catch (const zmq::error_t& e) {
            std::cerr << "[WidgetService] 发布事件失败: " << e.what() << std::endl;
        }
    }

    void handleTimerAutoShow(const json& payload) {
        std::cout << "[WidgetService] DEBUG: handleTimerAutoShow called, hidden_by_timeout=" << (timer_hidden_by_timeout_ ? "true" : "false") << std::endl;
        std::cout << "[WidgetService] DEBUG: handleTimerAutoShow called, hidden_by_timeout=" << (timer_hidden_by_timeout_ ? "true" : "false") << std::endl;
        if (!timer_hidden_by_timeout_) {
            return;
        }
        timer_hidden_by_timeout_ = false;

        int remaining = payload.value("remaining", -1);
        std::cout << "[WidgetService] Timer auto-show requested (remaining=" << remaining << ")" << std::endl;
        json show_payload = json::object();
        if (payload.contains("timeout_ms")) show_payload["timeout_ms"] = payload["timeout_ms"];
        handleShowWidget("timer", show_payload);
    }

    void publishStatus() {
        json status;
        status["lcd_active"] = lcd_active_;
        status["active_widget"] = active_widget_id_;
        
        auto* clock = widget_manager_.getWidget("clock");
        auto* timer = widget_manager_.getWidget("timer");
        
        status["clock"] = {
            {"enabled", clock ? clock->isEnabled() : false},
            {"core_enabled", clock ? clock->isCoreEnabled() : false}
        };
        status["timer"] = {
            {"enabled", timer ? timer->isEnabled() : false},
            {"core_enabled", timer ? timer->isCoreEnabled() : false}
        };
        
        auto* timer_w = dynamic_cast<TimerWidget*>(timer);
        if (timer_w) {
            status["timer"]["state"] = static_cast<int>(timer_w->getState());
            status["timer"]["elapsed_sec"] = timer_w->getElapsedSeconds();
            status["timer"]["remaining_sec"] = timer_w->getRemainingSeconds();
        }
        
        publishEvent("status.widget.state", status);
    }

    // ==================== ZMQ 命令订阅循环 ====================
    void subscriberLoop() {
        std::cout << "[WidgetService] 命令订阅线程启动" << std::endl;
        
        while (running_) {
            try {
                zmq::message_t topic_msg, payload_msg;
                
                auto result = zmq_sub_->recv(topic_msg, zmq::recv_flags::none);
                if (!result) continue; // 超时
                
                // 接收 payload
                auto res2 = zmq_sub_->recv(payload_msg, zmq::recv_flags::none);
                if (!res2) continue;
                
                std::string topic(static_cast<char*>(topic_msg.data()), topic_msg.size());
                std::string payload(static_cast<char*>(payload_msg.data()), payload_msg.size());
                
                std::cout << "[WidgetService] 收到命令: topic=" << topic << " payload=" << payload << std::endl;
                
                processCommand(topic, payload);
                
            } catch (const zmq::error_t& e) {
                if (e.num() == EAGAIN) continue; // 超时
                if (e.num() == ETERM) break;      // 上下文关闭
                std::cerr << "[WidgetService] ZMQ 接收错误: " << e.what() << std::endl;
            }
        }
        
        std::cout << "[WidgetService] 命令订阅线程退出" << std::endl;
    }

    // ==================== 命令处理 ====================
    void processCommand(const std::string& topic, const std::string& payload_str) {
        json payload;
        try {
            payload = json::parse(payload_str);
        } catch (const json::parse_error& e) {
            std::cerr << "[WidgetService] JSON 解析失败: " << e.what() << std::endl;
            return;
        }

        std::string widget_id = payload.value("widget_id", "");
        std::string action = payload.value("action", "");
        
        // 如果 topic 中包含 widget_id 信息
        if (widget_id.empty()) {
            if (topic.find("clock") != std::string::npos) widget_id = "clock";
            else if (topic.find("timer") != std::string::npos) widget_id = "timer";
        }
        
        // 如果 topic 中包含 action 信息
        if (action.empty()) {
            if (topic.find(".show") != std::string::npos) action = "show";
            else if (topic.find(".hide") != std::string::npos) action = "hide";
            else if (topic.find(".start") != std::string::npos) action = "start";
            else if (topic.find(".pause") != std::string::npos) action = "pause";
            else if (topic.find(".resume") != std::string::npos) action = "resume";
            else if (topic.find(".stop") != std::string::npos) action = "stop";
            else if (topic.find(".reset") != std::string::npos) action = "reset";
            else if (topic.find(".chime") != std::string::npos) action = "chime_now";
            else if (topic.find(".query") != std::string::npos) action = "get_time";
            else if (topic.find(".configure") != std::string::npos) action = "configure";
        }
        
        std::cout << "[WidgetService] 处理命令: widget=" << widget_id 
                  << " action=" << action << std::endl;

        if (action == "show") {
            handleShowWidget(widget_id, payload);
        } else if (action == "hide") {
            handleHideWidget(widget_id, payload);
        } else if (action == "start" || action == "pause" || action == "resume" 
                   || action == "stop" || action == "reset") {
            handleTimerCommand(widget_id, action, payload);
        } else if (action == "chime_now" || action == "announce_time" || action == "get_time") {
            handleClockCommand(action, payload);
        } else if (action == "configure") {
            handleConfigureWidget(widget_id, payload);
        } else {
            std::cerr << "[WidgetService] 未知命令: " << action << std::endl;
        }
    }

    // ==================== 显示 Widget ====================
    void handleShowWidget(const std::string& widget_id, const json& payload) {
        std::cout << "[WidgetService] handleShowWidget: " << widget_id << " payload=" << payload.dump() << std::endl;
        std::cout << "[WidgetService] handleShowWidget: " << widget_id << " payload=" << payload.dump() << std::endl;
        auto* widget = widget_manager_.getWidget(widget_id);
        if (!widget) {
            std::cerr << "[WidgetService] Widget 不存在: " << widget_id << std::endl;
            return;
        }

        if (widget_id == "timer") {
            timer_hidden_by_timeout_ = false;
        }

        // 获取 LCD 控制权
        if (!lcd_active_) {
            acquireLcd();
        }

        // 配置 widget
        if (payload.contains("config")) {
            widget->updateConfig(payload["config"].dump());
        }

        // 启用 widget
        widget->setEnabled(true);
        json show_config = payload.contains("config") ? payload["config"] : json::object();
        widget->onShow(show_config);
        active_widget_id_ = widget_id;
        
        // 设置超时
        uint32_t timeout_ms = payload.value("timeout_ms", 0u);
        if (timeout_ms > 0) {
            widget_timeout_ = std::chrono::steady_clock::now() + 
                              std::chrono::milliseconds(timeout_ms);
            has_timeout_ = true;
        } else {
            has_timeout_ = false;
        }

        std::cout << "[WidgetService] Widget 已显示: " << widget_id 
                  << " timeout_ms=" << timeout_ms << std::endl;
    }

    // ==================== 隐藏 Widget ====================
    void handleHideWidget(const std::string& widget_id, const json& /* payload */, bool keep_core_active = false) {
        if (!widget_id.empty()) {
            auto* widget = widget_manager_.getWidget(widget_id);
            if (widget) {
                widget->onHide();
                widget->setEnabled(false);
            }
        } else {
            // 隐藏所有 widget
            for (const auto& name : widget_manager_.getWidgetNames()) {
                auto* w = widget_manager_.getWidget(name);
                if (w && w->isEnabled()) {
                    w->onHide();
                    w->setEnabled(false);
                }
            }
        }

        active_widget_id_.clear();
        has_timeout_ = false;

        // 释放 LCD 控制权
        std::cout << "[WidgetService] 准备释放 LCD 控制权..." << std::endl;
        releaseLcd();
        
        std::cout << "[WidgetService] ✅ Widget 已完全隐藏: " << widget_id << std::endl;
        if (widget_id.empty()) {
            widget_manager_.setWidgetCoreEnabled("timer", false);
            timer_hidden_by_timeout_ = false;
        } else if (widget_id == "timer") {
            widget_manager_.setWidgetCoreEnabled("timer", keep_core_active);
            timer_hidden_by_timeout_ = keep_core_active;
        } else {
            timer_hidden_by_timeout_ = false;
        }
    }

    // ==================== 定时器命令 ====================
    void handleTimerCommand(const std::string& widget_id, const std::string& action, const json& payload) {
        auto* widget = widget_manager_.getWidget(widget_id.empty() ? "timer" : widget_id);
        auto* timer = dynamic_cast<TimerWidget*>(widget);
        if (!timer) {
            std::cerr << "[WidgetService] Timer widget 不存在" << std::endl;
            return;
        }

        if (action == "start") {
            if (payload.contains("mode")) {
                std::string mode = payload["mode"];
                timer->setMode(mode == "countup" ? TimerMode::Countup : TimerMode::Countdown);
            }
            if (payload.contains("duration_sec")) {
                timer->setDuration(payload["duration_sec"].get<int>());
            }
            if (payload.contains("auto_hide")) {
                timer->setAutoHide(payload["auto_hide"].get<bool>());
            }
            if (payload.contains("auto_show_remaining_sec")) {
                int auto_val = payload["auto_show_remaining_sec"].get<int>();
                std::cout << "[WidgetService] configuring timer auto_show_remaining_sec=" << auto_val << std::endl;
                json cfg;
                cfg["auto_show_remaining_sec"] = auto_val;
                timer->updateConfig(cfg.dump());
            }
            if (payload.contains("style")) {
                std::string style_str = payload["style"].dump();
                timer->updateConfig(style_str);
            }
            if (payload.contains("layout")) {
                json cfg;
                cfg["layout"] = payload["layout"];
                timer->updateConfig(cfg.dump());
            }

            widget_manager_.setWidgetCoreEnabled("timer", true);
            handleShowWidget(widget_id.empty() ? "timer" : widget_id, payload);
            timer->start();

            json started_evt = json::object();
            started_evt["mode"] = payload.value("mode", "countdown");
            started_evt["duration_sec"] = payload.value("duration_sec", 60);
            publishEvent("event.widget.timer.started", started_evt);

        } else if (action == "pause") {
            timer->pause();
            publishEvent("event.widget.timer.paused", {
                {"elapsed_sec", timer->getElapsedSeconds()},
                {"remaining_sec", timer->getRemainingSeconds()}
            });

        } else if (action == "resume") {
            timer->resume();
            publishEvent("event.widget.timer.resumed", {});

        } else if (action == "stop") {
            timer->stop();
            handleHideWidget(widget_id.empty() ? "timer" : widget_id, payload);
            publishEvent("event.widget.timer.stopped", {});

        } else if (action == "reset") {
            timer->resetTimer();
            publishEvent("event.widget.timer.reset", {});
        }
    }

    // ==================== 时钟命令 ====================
    void handleClockCommand(const std::string& action, const json& payload) {
        auto* widget = widget_manager_.getWidget("clock");
        auto* clock = dynamic_cast<ClockWidget*>(widget);
        if (!clock) {
            std::cerr << "[WidgetService] Clock widget 不存在" << std::endl;
            return;
        }

        if (action == "chime_now" || action == "announce_time") {
            // 通过更新配置触发报时（ClockWidget 的 chime 逻辑会在 update 中处理）
            json cfg;
            cfg["command"] = action;
            if (payload.contains("language")) {
                cfg["language"] = payload["language"];
            }
            clock->updateConfig(cfg.dump());

            publishEvent("event.widget.clock.chime", {
                {"action", action}
            });
        } else if (action == "get_time") {
            // 时间查询 - ClockWidget 有内置 API server
            // 这里也发布一个事件
            time_t now = time(nullptr);
            struct tm* tm = localtime(&now);
            publishEvent("event.widget.clock.time", {
                {"hour", tm->tm_hour},
                {"minute", tm->tm_min},
                {"second", tm->tm_sec}
            });
        }
    }

    // ==================== 配置 Widget ====================
    void handleConfigureWidget(const std::string& widget_id, const json& payload) {
        if (widget_id.empty()) return;
        
        std::string config_str;
        if (payload.contains("config")) {
            config_str = payload["config"].dump();
        } else {
            config_str = payload.dump();
        }
        
        if (widget_manager_.updateWidgetConfig(widget_id, config_str)) {
            std::cout << "[WidgetService] Widget 配置已更新: " << widget_id << std::endl;
        } else {
            std::cerr << "[WidgetService] Widget 配置更新失败: " << widget_id << std::endl;
        }
    }

    // ==================== LCD 获取/释放 ====================
    void acquireLcd() {
        if (lcd_active_) return;
        
        std::cout << "[WidgetService] 请求获取 LCD 控制权..." << std::endl;
        
        // 发布 LCD 获取请求事件 (让 eyeEngine 暂停渲染)
        publishEvent("event.widget.lcd_request", {
            {"action", "acquire"},
            {"widget_id", active_widget_id_}
        });
        
        // 等待一小段时间让 eyeEngine 响应
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        
        // 初始化 LCD (使用 LCD_12BIT 与 eyeEngine 保持一致)
        if (transport_ && !lcd_initialized_) {
            std::vector<DisplayConfig> configs;
            configs.push_back(DisplayConfig{LcdLeft});
            configs.push_back(DisplayConfig{LcdRight});
            
            if (transport_->init(LCD_12BIT, configs)) {
                lcd_initialized_ = true;
                std::cout << "[WidgetService] LCD 已初始化 (LCD_12BIT)" << std::endl;
            } else {
                std::cerr << "[WidgetService] LCD 初始化失败" << std::endl;
                return;
            }
        }
        
        lcd_active_ = true;
        
        // 发布 LCD 已获取事件
        publishEvent("event.widget.lcd_acquired", {
            {"widget_id", active_widget_id_},
            {"timestamp", std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::system_clock::now().time_since_epoch()).count()}
        });
        
        std::cout << "[WidgetService] LCD 控制权已获取" << std::endl;
    }

    void releaseLcd() {
        if (!lcd_active_) return;
        
        // 关闭 LCD
        if (transport_ && lcd_initialized_) {
            transport_->shutdown();
            lcd_initialized_ = false;
        }
        
        lcd_active_ = false;
        
        // 发布 LCD 释放事件
        publishEvent("event.widget.lcd_released", {
            {"widget_id", active_widget_id_},
            {"timestamp", std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::system_clock::now().time_since_epoch()).count()}
        });
        
        std::cout << "[WidgetService] LCD 控制权已释放" << std::endl;
    }

    // ==================== 渲染循环 ====================
    void renderLoop() {
        std::cout << "[WidgetService] 渲染线程启动" << std::endl;
        
        const double frame_interval_ms = 1000.0 / config_.render_fps;
        auto last_frame = std::chrono::steady_clock::now();
        auto last_status = last_frame;
        
        while (running_) {
            auto now = std::chrono::steady_clock::now();
            double dt_ms = std::chrono::duration<double, std::milli>(now - last_frame).count();
            
            if (dt_ms < frame_interval_ms) {
                std::this_thread::sleep_for(
                    std::chrono::milliseconds(static_cast<int>(frame_interval_ms - dt_ms)));
                continue;
            }
            last_frame = now;

            // 检查超时
            if (has_timeout_ && now >= widget_timeout_) {
                std::cout << "[WidgetService] Widget 显示超时，自动隐藏" << std::endl;
                std::string timeout_widget_id = active_widget_id_;
                if (timeout_widget_id.empty()) {
                    // Fallback: recover visible widget id to avoid hiding all widgets by mistake.
                    auto* timer_widget = widget_manager_.getWidget("timer");
                    auto* clock_widget = widget_manager_.getWidget("clock");
                    if (timer_widget && timer_widget->isEnabled()) {
                        timeout_widget_id = "timer";
                    } else if (clock_widget && clock_widget->isEnabled()) {
                        timeout_widget_id = "clock";
                    }
                    std::cout << "[WidgetService] timeout fallback widget_id=" << timeout_widget_id << std::endl;
                }

                bool was_timer = (timeout_widget_id == "timer");
                bool keep_core = false;
                if (was_timer) {
                    if (auto* timer_widget = dynamic_cast<TimerWidget*>(widget_manager_.getWidget("timer"))) {
                        keep_core = timer_widget->autoShowEnabled();
                    }
                }
                handleHideWidget(timeout_widget_id, json::object(), keep_core);
                has_timeout_ = false;
            }

            // 更新所有 widget（即使不显示，也执行核心逻辑如整点报时）
            widget_manager_.updateAll(dt_ms);

            // 处理 widget 内部自隐藏的情况，例如 timer 在 finished timeout 后自行隐藏。
            if (!active_widget_id_.empty()) {
                auto* active_widget = widget_manager_.getWidget(active_widget_id_);
                if (active_widget && !active_widget->isEnabled()) {
                    std::cout << "[WidgetService] active widget self-hidden: " << active_widget_id_ << std::endl;
                    if (active_widget_id_ == "timer") {
                        if (auto* timer_widget = dynamic_cast<TimerWidget*>(active_widget)) {
                            if (timer_widget->getState() == TimerState::Idle) {
                                widget_manager_.setWidgetCoreEnabled("timer", false);
                                timer_hidden_by_timeout_ = false;
                                std::cout << "[WidgetService] timer self-hide reached idle, core disabled" << std::endl;
                            }
                        }
                    }
                    active_widget_id_.clear();
                    has_timeout_ = false;
                    releaseLcd();
                }
            }

            // 如果有 widget 在显示且 LCD 可用，执行渲染
            if (lcd_active_ && transport_ && lcd_initialized_) {
                bool any_visible = false;
                for (const auto& name : widget_manager_.getWidgetNames()) {
                    auto* w = widget_manager_.getWidget(name);
                    if (w && w->isEnabled()) {
                        any_visible = true;
                        break;
                    }
                }

                if (any_visible) {
                    // 清空帧缓冲
                    frame_left_.clear();
                    frame_right_.clear();

                    // 渲染左眼
                    widget_manager_.renderAll(frame_left_, LcdLeft, true);
                    transport_->submitFrame(LcdLeft, frame_left_.data());

                    // 渲染右眼
                    widget_manager_.renderAll(frame_right_, LcdRight, true);
                    transport_->submitFrame(LcdRight, frame_right_.data());
                }
            }

            // 周期性发布状态 (每2秒)
            auto status_dt = std::chrono::duration<double>(now - last_status).count();
            if (status_dt >= 2.0) {
                publishStatus();
                last_status = now;
            }
        }
        
        std::cout << "[WidgetService] 渲染线程退出" << std::endl;
    }

    // ==================== 加载 Widget 配置 ====================
    void loadWidgetConfig() {
        // 尝试加载配置文件
        std::vector<std::string> config_paths = {
            config_.widgets_config_path,
            "/home/pi/dolydev/libs/widgets/config/widgets.default.json",
            "/home/pi/dolydev/libs/EyeEngine/config/widgets.default.json",
        };

        for (const auto& path : config_paths) {
            if (path.empty()) continue;
            std::ifstream f(path);
            if (f.is_open()) {
                try {
                    json cfg = json::parse(f);
                    
                    // 应用 clock 配置
                    if (cfg.contains("clock")) {
                        auto* clock = widget_manager_.getWidget("clock");
                        if (clock) {
                            clock->updateConfig(cfg["clock"].dump());
                            // 启用核心逻辑（后台整点报时等）
                            bool core_en = cfg["clock"].value("enabled", true);
                            clock->setCoreEnabled(core_en);
                            std::cout << "[WidgetService] Clock 配置已加载: " << path 
                                      << " core_enabled=" << core_en << std::endl;
                        }
                    }
                    
                    // 应用 timer 配置
                    if (cfg.contains("timer")) {
                        auto* timer = widget_manager_.getWidget("timer");
                        if (timer) {
                            timer->updateConfig(cfg["timer"].dump());
                            std::cout << "[WidgetService] Timer 配置已加载: " << path << std::endl;
                        }
                    }
                    
                    std::cout << "[WidgetService] 配置文件已加载: " << path << std::endl;
                    return;
                } catch (const json::parse_error& e) {
                    std::cerr << "[WidgetService] 配置文件解析失败: " << path 
                              << " - " << e.what() << std::endl;
                }
            }
        }
        
        std::cout << "[WidgetService] 未找到配置文件，使用默认配置" << std::endl;
    }

private:
    WidgetServiceConfig config_;
    bool initialized_ = false;
    std::atomic<bool> running_{false};

    // LCD
    std::unique_ptr<LcdTransport> transport_;
    bool lcd_initialized_ = false;
    bool lcd_active_ = false;
    FrameBuffer frame_left_;
    FrameBuffer frame_right_;
    std::unique_ptr<LcdMutex> lcd_mutex_;

    // Widget
    WidgetManager widget_manager_;
    std::string active_widget_id_;
    bool timer_hidden_by_timeout_ = false;
    bool has_timeout_ = false;
    std::chrono::steady_clock::time_point widget_timeout_;

    // ZMQ
    std::unique_ptr<zmq::context_t> zmq_ctx_;
    std::unique_ptr<zmq::socket_t> zmq_sub_;
    std::unique_ptr<zmq::socket_t> zmq_pub_;
    std::mutex pub_mutex_;

    // 线程
    std::thread sub_thread_;
    std::thread render_thread_;
};

// ======================== Main ========================
int main(int argc, char* argv[]) {
    // 注册信号处理
    signal(SIGINT, signalHandler);
    signal(SIGTERM, signalHandler);

    std::cout << "============================================" << std::endl;
    std::cout << "  Doly Widget Service v" << DOLY_WIDGET_SERVICE_VERSION << std::endl;
    std::cout << "============================================" << std::endl;

    // 解析命令行参数
    WidgetServiceConfig config;
    for (int i = 1; i < argc; i++) {
        std::string arg(argv[i]);
        if (arg == "--config" && i + 1 < argc) {
            config.widgets_config_path = argv[++i];
        } else if (arg == "--no-lcd") {
            config.no_lcd = true;
        } else if (arg == "--sub" && i + 1 < argc) {
            config.sub_endpoint = argv[++i];
        } else if (arg == "--pub" && i + 1 < argc) {
            config.pub_endpoint = argv[++i];
        } else if (arg == "--fps" && i + 1 < argc) {
            config.render_fps = std::atof(argv[++i]);
        } else if (arg == "--help" || arg == "-h") {
            std::cout << "用法: " << argv[0] << " [选项]" << std::endl;
            std::cout << "选项:" << std::endl;
            std::cout << "  --config <path>   Widget 配置文件路径" << std::endl;
            std::cout << "  --no-lcd          无 LCD 模式（仅逻辑运行）" << std::endl;
            std::cout << "  --sub <endpoint>  ZMQ SUB 端点" << std::endl;
            std::cout << "  --pub <endpoint>  ZMQ PUB 端点" << std::endl;
            std::cout << "  --fps <value>     渲染帧率" << std::endl;
            std::cout << "  --help, -h        显示帮助" << std::endl;
            return 0;
        }
    }

    // 如果指定了配置文件，尝试从中加载服务配置
    if (!config.widgets_config_path.empty()) {
        std::ifstream f(config.widgets_config_path);
        if (f.is_open()) {
            try {
                json cfg = json::parse(f);
                config = WidgetServiceConfig::fromJson(cfg);
                config.widgets_config_path = argv[2]; // 保留路径
            } catch (...) {}
        }
    }

    // 创建并启动服务
    WidgetService service;
    if (!service.initialize(config)) {
        std::cerr << "[WidgetService] 初始化失败" << std::endl;
        return 1;
    }

    if (!service.start()) {
        std::cerr << "[WidgetService] 启动失败" << std::endl;
        return 1;
    }

    // 主循环等待退出信号
    while (g_running && service.isRunning()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }

    service.stop();
    std::cout << "[WidgetService] 正常退出" << std::endl;
    return 0;
}
