#include "doly/vision/vision_bus_bridge.hpp"
#include <chrono>
#include <cstring>
#include <iostream>

namespace doly::vision {

namespace {
constexpr const char* kEventFace = "event.vision.face";
constexpr const char* kEventRecognition = "event.vision.face.recognized";
constexpr const char* kEventLost = "event.vision.face.lost";
constexpr const char* kEventCaptureStarted = "event.vision.capture.started";
constexpr const char* kEventCaptureComplete = "event.vision.capture.complete";
constexpr const char* kEventFaceRegistered = "event.vision.face.registered";
constexpr const char* kEventFaceUpdated = "event.vision.face.updated";
constexpr const char* kEventFaceDeleted = "event.vision.face.deleted";
constexpr const char* kStatusTopic = "status.vision.state";

inline std::chrono::milliseconds ms(int value) {
    return std::chrono::milliseconds(value);
}

}  // namespace

VisionBusBridge::VisionBusBridge(const Config& config,
                                 RuntimeControl& control,
                                 RuntimeMetrics& metrics,
                                 std::function<bool(RunMode)> mode_handler,
                                 std::function<nlohmann::json(const std::string&, const nlohmann::json&)> face_handler)
    : config_(config)
    , control_(control)
    , metrics_(metrics)
    , mode_handler_(std::move(mode_handler))
    , face_handler_(std::move(face_handler)) {
    context_ = std::make_unique<zmq::context_t>(1);
    current_mode_.store(config_.initial_mode);
    enable_eye_follow_.store(config_.enable_eye_follow);
    status_interval_ms_.store(config_.status_interval_ms);
    gaze_publish_interval_ms_.store(config_.gaze_publish_interval_ms);
    mode_timeout_seconds_ = config_.mode_timeout_seconds;
    enable_mode_timeout_ = config_.enable_mode_timeout;
    mode_received_.store(false);
}

VisionBusBridge::~VisionBusBridge() {
    stop();
}

bool VisionBusBridge::start() {
    if (running_.load()) {
        return false;
    }

    running_ = true;

    // 预先创建发布 socket（与 EyeExecutor 同样使用 connect 方式）
    ensurePublisher();

    sub_thread_ = std::thread(&VisionBusBridge::commandThread, this);
    status_thread_ = std::thread(&VisionBusBridge::statusThread, this);
    timeout_thread_ = std::thread(&VisionBusBridge::timeoutCheckThread, this);  // 🆕 启动超时检查线程
    applyMode(current_mode_.load(), config_.auto_start);  // 根据初始模式设置运行态
    return true;
}

void VisionBusBridge::stop() {
    if (!running_.exchange(false)) {
        return;
    }

    control_.requestShutdown();

    if (sub_thread_.joinable()) {
        sub_thread_.join();
    }
    if (status_thread_.joinable()) {
        status_thread_.join();
    }
    if (timeout_thread_.joinable()) {  // 🆕 停止超时检查线程
        timeout_thread_.join();
    }

    {
        std::lock_guard<std::mutex> lock(pub_mutex_);
        if (pub_socket_) {
            pub_socket_->close();
            pub_socket_.reset();
        }
    }

    if (context_) {
        context_->close();
    }
}

void VisionBusBridge::publishEvent(const std::string& topic, const nlohmann::json& data) {
    ensurePublisher();

    nlohmann::json envelope = wrapEnvelope(data);
    std::string payload = envelope.dump();

    std::lock_guard<std::mutex> lock(pub_mutex_);
    if (!pub_socket_) {
        return;
    }

    try {
        pub_socket_->send(zmq::buffer(topic), zmq::send_flags::sndmore);
        pub_socket_->send(zmq::buffer(payload), zmq::send_flags::none);
    } catch (const zmq::error_t& e) {
        std::cerr << "[VisionBusBridge] publishEvent error: " << e.what() << std::endl;
    }
}

void VisionBusBridge::publishStatusSnapshot() {
    nlohmann::json status;
    status["enabled"] = control_.isEnabled();
    status["streaming"] = control_.isStreamingEnabled();
    status["mode"] = runModeToString(current_mode_.load());
    status["fps"] = metrics_.fps.load();
    status["active_tracks"] = metrics_.active_tracks.load();
    status["recognized_count"] = metrics_.recognized_faces.load();
    publishEvent(kStatusTopic, status);
}

void VisionBusBridge::publishFaceSnapshot(const nlohmann::json& payload) {
    if (!isFaceOpsAllowed()) {
        return;
    }
    publishEvent(kEventFace, payload);
    if (enable_eye_follow_.load()) {
        std::lock_guard<std::mutex> lock(latest_face_mutex_);
        latest_primary_face_ = payload.value("primary", nlohmann::json::object());
    }
}

void VisionBusBridge::publishRecognitionEvent(const nlohmann::json& payload) {
    // 🔥 添加诊断日志
    bool allowed = isFaceOpsAllowed();
    std::cerr << "[VisionBusBridge::publishRecognitionEvent] 🔍 isFaceOpsAllowed()=" << (allowed ? "true" : "false")
              << ", mode_received=" << mode_received_.load() 
              << ", current_mode=" << runModeToString(current_mode_.load()) << std::endl;
    
    if (!allowed) {
        std::cerr << "[VisionBusBridge::publishRecognitionEvent] ⚠️  跳过发布: 人脸操作未被允许" << std::endl;
        return;
    }
    publishEvent(kEventRecognition, payload);
}

void VisionBusBridge::publishFaceLostEvent(const nlohmann::json& payload) {
    if (!isFaceOpsAllowed()) {
        return;
    }
    publishEvent(kEventLost, payload);
}

void VisionBusBridge::publishCaptureStarted(const nlohmann::json& payload) {
    publishEvent(kEventCaptureStarted, payload);
}

void VisionBusBridge::publishCaptureComplete(const nlohmann::json& payload) {
    publishEvent(kEventCaptureComplete, payload);
}

void VisionBusBridge::publishCaptureResult(const nlohmann::json& payload) {
    publishCaptureComplete(payload);
}

void VisionBusBridge::updateLatestFace(const nlohmann::json& primary_face) {
    if (!enable_eye_follow_.load() || !isFaceOpsAllowed()) {
        return;
    }
    {
        std::lock_guard<std::mutex> lock(latest_face_mutex_);
        latest_primary_face_ = primary_face;
    }
    sendEyeGazeCommand(primary_face);
    sendMotorTrackingCommand(primary_face);  // 📌 Task 2-3: 调用电机跟踪
}

void VisionBusBridge::commandThread() {
    std::cerr << "[VisionBusBridge::commandThread] ⭐ 启动命令线程" << std::endl;
    std::cerr << "[VisionBusBridge::commandThread] 尝试连接到: " << config_.sub_endpoint << std::endl;
    
    zmq::socket_t sub_sock(*context_, zmq::socket_type::sub);
    sub_sock.connect(config_.sub_endpoint);
    std::cerr << "[VisionBusBridge::commandThread] ✅ 已连接到 SUB 端点" << std::endl;

    const char* prefixes[] = {
        "cmd.vision",
        "cmd.eye.follow",
        "event.audio.cmd_ActWhoami"  // 🆕 监听"看看我是谁"命令
    };
    for (auto prefix : prefixes) {
        sub_sock.setsockopt(ZMQ_SUBSCRIBE, prefix, std::strlen(prefix));
        std::cerr << "[VisionBusBridge::commandThread] ✅ 已订阅前缀: " << prefix << std::endl;
    }

    const int timeout_ms = 250;
    sub_sock.setsockopt(ZMQ_RCVTIMEO, timeout_ms);
    std::cerr << "[VisionBusBridge::commandThread] ✅ 接收超时设置为: " << timeout_ms << "ms" << std::endl;
    std::cerr << "[VisionBusBridge::commandThread] 🔄 commandThread 已初始化，开始监听命令..." << std::endl;

    // 【新增】广播就绪状态，通知 Daemon 可以开始发送模式命令了
    try {
        nlohmann::json ready_data;
        ready_data["status"] = "online";
        ready_data["service"] = "vision";
        publishEvent("status.vision.ready", ready_data);
    } catch (...) {}

    int heartbeat_counter = 0;
    while (running_.load()) {
        try {
            zmq::message_t topic_msg;
            if (!sub_sock.recv(topic_msg)) {
                // 超时是正常的
                heartbeat_counter++;
                if (heartbeat_counter % 40 == 0) {  // 每10秒左右打印一次心跳（40 * 250ms = 10s）
                    std::cerr << "[VisionBusBridge::commandThread] 💓 心跳：等待命令..." << std::endl;
                }
                continue;
            }
            heartbeat_counter = 0;

            // 检查是否有更多帧的小工具
            auto hasMore = [&sub_sock]() -> bool {
                int more = 0;
                size_t more_size = sizeof(more);
                sub_sock.getsockopt(ZMQ_RCVMORE, &more, &more_size);
                return more > 0;
            };

            std::string first_part(static_cast<char*>(topic_msg.data()), topic_msg.size());
            std::string topic;
            std::string payload;

            // 重要：鲁棒地处理多帧和单帧
            if (hasMore()) {
                // 标准 ZMQ 多帧模式 (Part 1: Topic, Part 2: Payload)
                topic = first_part;
                zmq::message_t payload_msg;
                if (sub_sock.recv(payload_msg)) {
                    payload = std::string(static_cast<char*>(payload_msg.data()), payload_msg.size());
                    // 消耗可能残留的冗余帧，防止 desync
                    while (hasMore()) {
                        zmq::message_t dummy;
                        sub_sock.recv(dummy);
                    }
                }
            } else {
                // 兼容模式：单帧格式 "topic payload" 或仅 topic
                size_t space_pos = first_part.find(' ');
                if (space_pos != std::string::npos) {
                    topic = first_part.substr(0, space_pos);
                    payload = first_part.substr(space_pos + 1);
                } else {
                    topic = first_part;
                    payload = "{}";
                }
            }

            // 使用简单字符防止编码问题日志可见
            std::cerr << "[VisionBusBridge::commandThread] [RECV] Topic: " << topic << std::endl;

            auto trim_payload = [](const std::string& value) -> std::string {
                const auto begin = value.find_first_not_of(" \t\r\n");
                if (begin == std::string::npos) {
                    return "";
                }
                const auto end = value.find_last_not_of(" \t\r\n");
                return value.substr(begin, end - begin + 1);
            };

            payload = trim_payload(payload);
            if (!payload.empty() && payload[0] != '{' && payload[0] != '[') {
                continue;
            }

            nlohmann::json message;
            try {
                message = nlohmann::json::parse(payload.empty() ? "{}" : payload);
            } catch (const nlohmann::json::exception& e) {
                std::cerr << "[VisionBusBridge] JSON parse error: " << e.what() << std::endl;
                if (payload.size() < 100) {
                     std::cerr << "[VisionBusBridge] Invalid Payload: " << payload << std::endl;
                }
                continue;
            }

            nlohmann::json data;
            if (message.contains("data")) {
                data = message["data"];
            } else {
                data = message;
            }

            if (topic == "cmd.vision.enable") {
                bool enabled = data.value("enabled", true);
                control_.setEnabled(enabled);
            } else if (topic == "cmd.vision.stream") {
                bool streaming = data.value("enabled", true);
                control_.setStreamingEnabled(streaming);
            } else if (topic == "cmd.vision.mode") {
                std::string mode_text = data.value("mode", std::string("FULL"));
                
                // ★ 增强日志：记录接收到的完整数据
                std::cerr << "[VisionBusBridge::commandThread] 📩 模式切换请求: " << mode_text 
                          << " | 完整消息: " << message.dump() << std::endl;
                
                auto mode = stringToRunMode(mode_text);
                
                // 🔄 同步处理但添加日志
                std::cerr << "[VisionBusBridge] ⏳ 开始模式切换: " << mode_text << std::endl;
                
                bool handled = false;
                if (mode_handler_) {
                    std::cerr << "[VisionBusBridge] 🔧 调用 mode_handler..." << std::endl;
                    handled = mode_handler_(mode);
                    std::cerr << "[VisionBusBridge] ✅ mode_handler 返回: " << (handled ? "true" : "false") << std::endl;
                }
                
                if (handled || !mode_handler_) {
                    std::cerr << "[VisionBusBridge] 🎯 应用模式: " << mode_text << std::endl;
                    applyMode(mode, true);
                    std::cerr << "[VisionBusBridge] ✅ 模式已应用: " << mode_text << std::endl;
                }
            } else if (topic == "cmd.vision.capture" || topic == "cmd.vision.capture.photo") {
                std::cerr << "[PhotoCmd-1] 📸 接收到拍照命令: topic=" << topic << std::endl;
                try {
                    std::cerr << "[PhotoCmd-2] 数据: " << data.dump(2) << std::endl;
                } catch (...) {}

                CaptureRequest request;
                request.request_id = data.value("request_id", std::string("capture"));
                request.params = data;
                request.params["type"] = "photo";
                if (!request.params.contains("save_snapshot")) {
                    request.params["save_snapshot"] = true;
                }
                
                std::cerr << "[PhotoCmd-3] 🔄 入队拍照请求: request_id=" << request.request_id << std::endl;
                control_.enqueueCaptureRequest(request);
                std::cerr << "[PhotoCmd-4] ✅ 拍照请求已入队" << std::endl;
                std::cerr << std::flush;
            } else if (topic == "cmd.vision.capture.video.start") {
                CaptureRequest request;
                request.request_id = data.value("request_id", std::string("video"));
                request.params = data;
                request.params["type"] = "video_start";
                control_.enqueueCaptureRequest(request);
            } else if (topic == "cmd.vision.capture.video.stop") {
                CaptureRequest request;
                request.request_id = data.value("request_id", std::string("video"));
                request.params = data;
                request.params["type"] = "video_stop";
                control_.enqueueCaptureRequest(request);
            } else if (topic == "cmd.vision.face.register" ||
                       topic == "cmd.vision.face.update" ||
                       topic == "cmd.vision.face.delete" ||
                       topic == "cmd.vision.face.recapture") {
                std::cerr << "[FaceCmd-1] 📩 接收到人脸管理命令: topic=" << topic << std::endl;
                try {
                    std::cerr << "[FaceCmd-2] 数据: " << data.dump(2) << std::endl;
                } catch (...) {}

                if (!face_handler_) {
                    std::cerr << "[FaceCmd-3] ⚠️ face_handler_ 未设置，跳过处理" << std::endl;
                    continue;
                }

                std::cerr << "[FaceCmd-4] 🔄 调用 face_handler_..." << std::endl;
                auto result = face_handler_(topic, data);

                try {
                    std::cerr << "[FaceCmd-5] ✅ face_handler_ 返回: " << result.dump(2) << std::endl;
                } catch (...) {}

                if (topic == "cmd.vision.face.register") {
                    std::cerr << "[FaceCmd-6] 📤 发布注册事件: " << kEventFaceRegistered << std::endl;
                    publishEvent(kEventFaceRegistered, result);
                    std::cerr << "[FaceCmd-7] ✅ 事件已发布" << std::endl;
                } else if (topic == "cmd.vision.face.update" || topic == "cmd.vision.face.recapture") {
                    publishEvent(kEventFaceUpdated, result);
                } else if (topic == "cmd.vision.face.delete") {
                    publishEvent(kEventFaceDeleted, result);
                }
                
                // 确保日志刷新
                std::cerr << std::flush;
            } else if (topic == "cmd.eye.follow") {
                // 允许外部配置 eye follow 行为
                enable_eye_follow_.store(data.value("enable", true));
            } else if (topic == "event.audio.cmd_ActWhoami") {
                // 🆕 处理"看看我是谁"命令：切换到 FULL 模式
                std::cerr << "[VisionBusBridge] 👤 收到'看看我是谁'命令，切换到 FULL 模式" << std::endl;
                bool handled = false;
                if (mode_handler_) {
                    handled = mode_handler_(RunMode::FULL);
                }
                if (handled || !mode_handler_) {
                    applyMode(RunMode::FULL, true);
                    std::cerr << "[VisionBusBridge] 🔄 模式切换: ??? → FULL (响应 cmd_ActWhoami)" << std::endl;
                }
            }
        } catch (const zmq::error_t& e) {
            if (e.num() == ETERM || e.num() == EINTR) {
                break;
            }
            // 其他错误仅记录日志
            std::cerr << "[VisionBusBridge] commandThread error: " << e.what() << std::endl;
        }
    }
}

void VisionBusBridge::statusThread() {
    while (running_.load()) {
        auto interval = ms(status_interval_ms_.load());
        std::this_thread::sleep_for(interval);
        publishStatusSnapshot();

        if (!enable_eye_follow_.load() || !isFaceOpsAllowed()) {
            continue;
        }

        nlohmann::json latest_face;
        {
            std::lock_guard<std::mutex> lock(latest_face_mutex_);
            if (!latest_primary_face_.has_value()) {
                continue;
            }
            latest_face = latest_primary_face_.value();
        }
        sendEyeGazeCommand(latest_face);
        sendMotorTrackingCommand(latest_face);  // 📌 Task 2-3: 调用电机跟踪
    }
}

void VisionBusBridge::ensurePublisher() {
    std::lock_guard<std::mutex> lock(pub_mutex_);
    if (pub_socket_) {
        return;
    }

    pub_socket_ = std::make_unique<zmq::socket_t>(*context_, zmq::socket_type::pub);
    // ★ ZMQ PUB-SUB 模式：Publisher 必须 BIND，Subscriber 必须 CONNECT
    pub_socket_->bind(config_.pub_endpoint);
    std::cout << "[VisionBusBridge] 📡 Publisher 已绑定到: " << config_.pub_endpoint << std::endl;
    // ZeroMQ 慢连接：等待一小段时间
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
}

void VisionBusBridge::sendEyeGazeCommand(const nlohmann::json& primary_face) {
    if (!enable_eye_follow_.load() || !isFaceOpsAllowed()) {
        return;
    }

    auto now = std::chrono::steady_clock::now();
    if (std::chrono::duration_cast<std::chrono::milliseconds>(now - last_gaze_publish_).count() <
        gaze_publish_interval_ms_.load()) {
        return;
    }

    if (!primary_face.contains("normalized")) {
        return;
    }

    double nx = primary_face["normalized"].value("x", 0.5);
    double ny = primary_face["normalized"].value("y", 0.5);

    // 📌 Task 2-2: 应用死区检查（防止微小抖动）
    double dx = nx - 0.5;
    double dy = ny - 0.5;
    double distance_from_center = std::sqrt(dx * dx + dy * dy);
    
    if (distance_from_center < config_.gaze_dead_zone) {
        // 在死区内，保持中心位置
        nx = 0.5;
        ny = 0.5;
    }
    
    // 📌 Task 2-2: 应用平滑系数
    if (config_.gaze_smoothing > 0.0f && config_.gaze_smoothing < 1.0f) {
        // 使用指数移动平均
        static double smoothed_x = 0.5;
        static double smoothed_y = 0.5;
        
        smoothed_x = smoothed_x * (1.0 - config_.gaze_smoothing) + nx * config_.gaze_smoothing;
        smoothed_y = smoothed_y * (1.0 - config_.gaze_smoothing) + ny * config_.gaze_smoothing;
        
        nx = smoothed_x;
        ny = smoothed_y;
    }

    nlohmann::json payload;
    payload["type"] = "custom";
    payload["x"] = nx;
    payload["y"] = ny;
    publishEvent("cmd.eye.gaze", payload);

    last_gaze_publish_ = now;
}

void VisionBusBridge::sendMotorTrackingCommand(const nlohmann::json& primary_face) {
    // 📌 Task 2-3: 电机跟踪控制
    if (!config_.motor_enabled || config_.tracking_mode == "disabled" || !isFaceOpsAllowed()) {
        return;
    }

    if (!primary_face.contains("normalized")) {
        return;
    }

    double nx = primary_face["normalized"].value("x", 0.5);
    
    // 计算与中心的横向偏差
    double offset = nx - 0.5;  // 范围：[-0.5, 0.5]
    
    // 检查触发阈值
    if (std::abs(offset) < config_.motor_trigger_threshold) {
        return;  // 在阈值内，不需要转身
    }
    
    // 决定转向方向
    int direction = (offset > 0) ? 1 : -1;  // 1: 右转, -1: 左转
    
    // 构造电机转身命令
    nlohmann::json payload;
    payload["direction"] = direction;           // 转向方向
    payload["speed"] = config_.motor_speed;      // 转身速度
    payload["offset"] = offset;                  // 规范化的偏移量
    payload["trigger_threshold"] = config_.motor_trigger_threshold;
    
    std::cerr << "[VisionBusBridge] 📍 电机跟踪命令: 方向=" << direction 
              << ", 速度=" << config_.motor_speed 
              << ", 偏移=" << offset << std::endl;
    
    publishEvent("cmd.motor.track", payload);
}

void VisionBusBridge::updateRuntimeConfig(const Config& config) {
    if (config.pub_endpoint != config_.pub_endpoint || config.sub_endpoint != config_.sub_endpoint) {
        std::cerr << "[VisionBusBridge] endpoint change detected (ignored at runtime)." << std::endl;
    }

    config_.enable_eye_follow = config.enable_eye_follow;
    config_.status_interval_ms = config.status_interval_ms;
    config_.gaze_publish_interval_ms = config.gaze_publish_interval_ms;
    config_.initial_mode = config.initial_mode;
    
    // 📌 更新人脸跟踪配置
    config_.tracking_mode = config.tracking_mode;
    config_.gaze_enabled = config.gaze_enabled;
    config_.gaze_smoothing = config.gaze_smoothing;
    config_.gaze_dead_zone = config.gaze_dead_zone;
    config_.motor_enabled = config.motor_enabled;
    config_.motor_trigger_threshold = config.motor_trigger_threshold;
    config_.motor_speed = config.motor_speed;

    mode_timeout_seconds_ = config.mode_timeout_seconds;
    enable_mode_timeout_ = config.enable_mode_timeout;

    enable_eye_follow_.store(config.enable_eye_follow);
    status_interval_ms_.store(config.status_interval_ms);
    gaze_publish_interval_ms_.store(config.gaze_publish_interval_ms);
}

void VisionBusBridge::timeoutCheckThread() {
    // 🆕 超时检查线程：定期检查 FULL 模式是否超时
    std::cerr << "[VisionBusBridge::timeoutCheckThread] ⭐ 启动超时检查线程 (超时时间: "
              << mode_timeout_seconds_ << "s)" << std::endl;
    
    while (running_.load()) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
        
        if (!enable_mode_timeout_) {
            continue;
        }
        
        RunMode current = current_mode_.load();
        if (current != RunMode::FULL) {
            continue;  // 仅在 FULL 模式下检查超时
        }
        
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - mode_start_time_).count();
        
        if (elapsed >= mode_timeout_seconds_) {
            std::cerr << "[VisionBusBridge] ⏰ FULL 模式超时（" << elapsed << "s >= " 
                      << mode_timeout_seconds_ << "s），自动恢复到 IDLE 模式" << std::endl;
            
            bool handled = false;
            if (mode_handler_) {
                handled = mode_handler_(RunMode::IDLE);
            }
            if (handled || !mode_handler_) {
                current_mode_.store(RunMode::IDLE);
                publishStatusSnapshot();
                std::cerr << "[VisionBusBridge] 🔄 模式切换: FULL → IDLE (超时恢复)" << std::endl;
            }
        }
    }
}

void VisionBusBridge::applyMode(RunMode mode, bool mark_mode_received) {
    current_mode_.store(mode);
    if (mark_mode_received) {
        mode_received_.store(true);
    }

    // 根据模式控制检测与推流
    switch (mode) {
        case RunMode::IDLE:
            control_.setEnabled(false);
            control_.setStreamingEnabled(false);
            break;
        case RunMode::STREAM_ONLY:
            control_.setEnabled(false);
            control_.setStreamingEnabled(true);
            break;
        case RunMode::DETECT_ONLY:
        case RunMode::DETECT_TRACK:
            control_.setEnabled(true);
            control_.setStreamingEnabled(false);
            break;
        case RunMode::FULL:
        case RunMode::CUSTOM:
            control_.setEnabled(true);
            control_.setStreamingEnabled(true);
            break;
    }

    mode_start_time_ = std::chrono::steady_clock::now();
    publishStatusSnapshot();
}

bool VisionBusBridge::isFaceOpsAllowed() const {
    if (!mode_received_.load()) {
        return false;
    }
    RunMode mode = current_mode_.load();
    return mode == RunMode::FULL || mode == RunMode::DETECT_ONLY ||
           mode == RunMode::DETECT_TRACK || mode == RunMode::CUSTOM;
}

bool VisionBusBridge::isRecognitionAllowed() const {
    if (!mode_received_.load()) {
        return false;
    }
    RunMode mode = current_mode_.load();
    return mode == RunMode::FULL;
}

bool VisionBusBridge::hasModeSignal() const {
    return mode_received_.load();
}

nlohmann::json VisionBusBridge::wrapEnvelope(const nlohmann::json& data) const {
    using clock = std::chrono::system_clock;
    auto now = clock::now();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();

    nlohmann::json envelope;
    envelope["ts"] = ms;
    envelope["src"] = config_.source_id;
    envelope["data"] = data;
    return envelope;
}

}  // namespace doly::vision
