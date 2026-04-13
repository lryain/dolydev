#include "doly/vision/vision_service.hpp"

#include "config.hpp"
#include "doly/vision/yaml_config.hpp"
#include "livefacereco.hpp"
#include "vision/noop_module.hpp"

#include <chrono>
#include <filesystem>
#include <iomanip>
#include <sstream>
#include <iostream>

namespace doly::vision {

VisionService::VisionService(Options options)
    : options_(std::move(options))
    , face_db_("/home/pi/dolydev/libslibs/FaceReco/data/face_db.json") {}

bool VisionService::loadSettings() {
    if (options_.config_path.empty()) {
        return true;
    }

    if (!Settings::load(options_.config_path)) {
        std::cerr << "⚠️  无法加载配置文件: " << options_.config_path
                  << " (将使用默认参数)" << std::endl;
        return false;
    }

    std::cout << "✅ 配置文件已加载: " << options_.config_path << std::endl;
    return true;
}

void VisionService::applyRuntimeConfig() {
    // 优先加载 YAML 可调配置
    std::filesystem::path yaml_path = std::filesystem::path(options_.config_path).parent_path() / "config/vision_service.yaml";
    if (!YAMLConfig::load(yaml_path.string())) {
        std::cerr << "[VisionService] ⚠️ 加载 YAML 配置失败，回退到 INI" << std::endl;
    }

    // YAML 中的总线/模式配置（可被守护进程调整）
    bus_config_ = YAMLConfig::loadBusConfig();
    bus_config_.pub_endpoint = YAMLConfig::getString("zmq.pub_endpoint", bus_config_.pub_endpoint);
    bus_config_.sub_endpoint = YAMLConfig::getString("bus.sub_endpoint", bus_config_.sub_endpoint);
    bus_config_.enable_eye_follow = YAMLConfig::getBool("gaze_follow.enabled", bus_config_.enable_eye_follow);
    bus_config_.status_interval_ms = YAMLConfig::getInt("service.status_interval_ms", bus_config_.status_interval_ms);
    bus_config_.gaze_publish_interval_ms = YAMLConfig::getInt("gaze_follow.publish_interval_ms", bus_config_.gaze_publish_interval_ms);

    // 使用 YAML 的人脸跟踪配置
    bus_config_.tracking_mode = YAMLConfig::getString("face_tracking_control.tracking_mode", bus_config_.tracking_mode);
    bus_config_.gaze_enabled = YAMLConfig::getBool("face_tracking_control.gaze_tracking.enabled", bus_config_.gaze_enabled);
    bus_config_.gaze_smoothing = YAMLConfig::getFloat("face_tracking_control.gaze_tracking.smoothing", bus_config_.gaze_smoothing);
    bus_config_.gaze_dead_zone = YAMLConfig::getFloat("face_tracking_control.gaze_tracking.dead_zone", bus_config_.gaze_dead_zone);
    bus_config_.motor_enabled = YAMLConfig::getBool("face_tracking_control.motor_tracking.enabled", bus_config_.motor_enabled);
    bus_config_.motor_trigger_threshold = YAMLConfig::getFloat("face_tracking_control.motor_tracking.trigger_threshold", bus_config_.motor_trigger_threshold);
    bus_config_.motor_speed = YAMLConfig::getInt("face_tracking_control.motor_tracking.turn_speed", bus_config_.motor_speed);

    // 🔍 打印初始模式配置
    std::cout << "[VisionService] 🎯 初始模式配置: " << runModeToString(bus_config_.initial_mode) << std::endl;

    std::cout << "[VisionService] 👁️ 人脸跟踪配置已加载:"
              << "\n  - 模式: " << bus_config_.tracking_mode
              << "\n  - 眼睛跟踪: " << (bus_config_.gaze_enabled ? "启用" : "禁用") 
              << " (平滑: " << bus_config_.gaze_smoothing << ", 死区: " << bus_config_.gaze_dead_zone << ")"
              << "\n  - 电机跟踪: " << (bus_config_.motor_enabled ? "启用" : "禁用")
              << " (阈值: " << bus_config_.motor_trigger_threshold << ", 速度: " << bus_config_.motor_speed << ")"
              << std::endl;

    query_endpoint_ = YAMLConfig::getString("zmq.query_endpoint", "ipc:///tmp/doly_vision_query.sock");

    auto db_path = Settings::getString(
        "vision_face_db_path",
        YAMLConfig::getString("project.path", "/home/pi/dolydev/libslibs/FaceReco") + "/data/face_db.json");
    face_db_.setStoragePath(db_path);
    face_db_.load();

    // 允许运行时配置控制启用与推流开关（默认保持当前状态）
    bool auto_start = bus_config_.auto_start;
    bool stream_default = YAMLConfig::getBool("video_stream.enable_to_eyeengine", runtime_control_.isStreamingEnabled());
    bool should_enable = auto_start && bus_config_.initial_mode != RunMode::IDLE;
    runtime_control_.setEnabled(should_enable);
    runtime_control_.setStreamingEnabled(auto_start && stream_default);

    module_manager_.setMode(bus_config_.initial_mode);
    std::cout << "[VisionService] 🔄 模块管理器已设置为: " << runModeToString(bus_config_.initial_mode) << std::endl;

    if (bus_bridge_) {
        bus_bridge_->updateRuntimeConfig(bus_config_);
        bus_bridge_->applyMode(bus_config_.initial_mode, auto_start);
    }
}

void VisionService::registerModules() {
    auto register_noop = [this](const std::string& key,
                                const std::vector<std::string>& deps = {}) {
        return module_manager_.registerModule(
            key,
            [key]() { return std::make_shared<NoopModule>(key); },
            deps);
    };

    register_noop("camera_capture");
    register_noop("video_stream", {"camera_capture"});
    register_noop("face_detection", {"camera_capture"});
    register_noop("face_tracking", {"face_detection"});
    register_noop("face_recognition", {"face_tracking"});
    register_noop("liveness_detection", {"face_detection"});
    register_noop("photo_capture", {"camera_capture"});
    // ✨ video_record 不再作为独立模块，而是在 livefacereco 主线程中处理
    // (视频录制逻辑已在 livefacereco.cpp 中完整实现)
}

bool VisionService::startBusBridge() {
    bus_bridge_ = std::make_unique<VisionBusBridge>(
        bus_config_,
        runtime_control_,
        runtime_metrics_,
        [this](RunMode mode) { return module_manager_.setMode(mode); },
        [this](const std::string& topic, const nlohmann::json& data) {
            return handleFaceCommand(topic, data);
        });

    return bus_bridge_->start();
}

void VisionService::stopBusBridge() {
    if (bus_bridge_) {
        bus_bridge_->stop();
        bus_bridge_.reset();
    }
}

void VisionService::startQueryServer() {
    query_running_.store(true);
    query_context_ = std::make_unique<zmq::context_t>(1);
    query_socket_ = std::make_unique<zmq::socket_t>(*query_context_, zmq::socket_type::rep);
    query_socket_->bind(query_endpoint_);
    query_socket_->setsockopt(ZMQ_RCVTIMEO, 200);
    query_thread_ = std::thread(&VisionService::queryLoop, this);
}

void VisionService::stopQueryServer() {
    if (!query_running_.exchange(false)) {
        return;
    }
    if (query_thread_.joinable()) {
        query_thread_.join();
    }
    if (query_socket_) {
        query_socket_->close();
        query_socket_.reset();
    }
    if (query_context_) {
        query_context_->close();
        query_context_.reset();
    }
}

void VisionService::queryLoop() {
    while (query_running_.load()) {
        zmq::message_t topic_msg;
        if (!query_socket_->recv(topic_msg)) {
            continue;
        }
        std::string topic(static_cast<char*>(topic_msg.data()), topic_msg.size());

        std::string payload;
        zmq::message_t payload_msg;
        if (query_socket_->recv(payload_msg, zmq::recv_flags::dontwait)) {
            payload.assign(static_cast<char*>(payload_msg.data()), payload_msg.size());
        } else {
            payload.assign(static_cast<char*>(topic_msg.data()), topic_msg.size());
            topic.clear();
        }

        nlohmann::json request;
        try {
            request = nlohmann::json::parse(payload);
        } catch (const nlohmann::json::exception&) {
            nlohmann::json error{{"success", false}, {"error", "invalid json"}};
            auto text = error.dump();
            query_socket_->send(zmq::buffer(text), zmq::send_flags::none);
            continue;
        }

        if (topic.empty()) {
            topic = request.value("topic", std::string(""));
        }

        auto response = handleQuery(topic, request);
        auto text = response.dump();
        query_socket_->send(zmq::buffer(text), zmq::send_flags::none);
    }
}

static std::string nowIso() {
    auto now = std::chrono::system_clock::now();
    auto t = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
    gmtime_r(&t, &tm);
    std::ostringstream oss;
    oss << std::put_time(&tm, "%Y-%m-%dT%H:%M:%SZ");
    return oss.str();
}

static std::string makeFaceId() {
    auto now = std::chrono::system_clock::now().time_since_epoch();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now).count();
    return "face_" + std::to_string(ms);
}

nlohmann::json VisionService::handleFaceCommand(const std::string& topic, const nlohmann::json& data) {
    nlohmann::json result;
    result["success"] = false;

    auto request_id = data.value("request_id", std::string());
    if (!request_id.empty()) {
        result["request_id"] = request_id;
    }

    if (topic == "cmd.vision.face.register") {
        auto method = data.value("method", std::string("current"));
        auto name = data.value("name", std::string());
        if (name.empty()) {
            result["message"] = "missing name";
            return result;
        }
        FaceRecord record;
        record.face_id = makeFaceId();
        record.name = name;
        record.metadata = data.value("metadata", nlohmann::json::object());
        record.image_path = data.value("file_path", std::string());
        record.created_at = nowIso();
        record.last_seen = record.created_at;
        record.sample_count = 1;

        if (method != "current" && method != "file") {
            result["message"] = "unsupported method";
            return result;
        }

        if (!face_db_.addOrUpdate(record) || !face_db_.save()) {
            result["message"] = "db write failed";
            return result;
        }

        result["success"] = true;
        result["face_id"] = record.face_id;
        result["name"] = record.name;
        result["message"] = "人脸注册成功";
        return result;
    }

    if (topic == "cmd.vision.face.update") {
        auto face_id = data.value("face_id", std::string());
        auto name = data.value("name", std::string());
        auto updates = data.value("updates", nlohmann::json::object());

        std::optional<FaceRecord> record;
        if (!face_id.empty()) {
            record = face_db_.get(face_id);
        } else if (!name.empty()) {
            record = face_db_.getByName(name);
        }
        if (!record.has_value()) {
            result["message"] = "face not found";
            return result;
        }

        std::vector<std::string> updated_fields;
        if (updates.contains("name")) {
            record->name = updates.value("name", record->name);
            updated_fields.push_back("name");
        }
        if (updates.contains("metadata")) {
            record->metadata = updates["metadata"];
            updated_fields.push_back("metadata");
        }
        record->last_seen = nowIso();

        if (!face_db_.addOrUpdate(*record) || !face_db_.save()) {
            result["message"] = "db write failed";
            return result;
        }

        result["success"] = true;
        result["face_id"] = record->face_id;
        result["updated_fields"] = updated_fields;
        return result;
    }

    if (topic == "cmd.vision.face.delete") {
        auto face_id = data.value("face_id", std::string());
        auto name = data.value("name", std::string());
        std::optional<FaceRecord> record;
        if (!face_id.empty()) {
            record = face_db_.get(face_id);
        } else if (!name.empty()) {
            record = face_db_.getByName(name);
        }
        if (!record.has_value()) {
            result["message"] = "face not found";
            return result;
        }

        if (!face_db_.remove(record->face_id) || !face_db_.save()) {
            result["message"] = "db write failed";
            return result;
        }

        result["success"] = true;
        result["face_id"] = record->face_id;
        result["name"] = record->name;
        result["deleted_files"] = 0;
        return result;
    }

    if (topic == "cmd.vision.face.recapture") {
        auto face_id = data.value("face_id", std::string());
        if (face_id.empty()) {
            result["message"] = "missing face_id";
            return result;
        }
        auto record = face_db_.get(face_id);
        if (!record.has_value()) {
            result["message"] = "face not found";
            return result;
        }
        record->sample_count += 1;
        record->last_seen = nowIso();
        if (!face_db_.addOrUpdate(*record) || !face_db_.save()) {
            result["message"] = "db write failed";
            return result;
        }
        result["success"] = true;
        result["face_id"] = record->face_id;
        result["total_samples"] = record->sample_count;
        return result;
    }

    result["message"] = "unsupported command";
    return result;
}

static bool matchesFilter(const FaceRecord& record, const nlohmann::json& filter) {
    if (!filter.is_object() || filter.empty()) {
        return true;
    }
    for (auto it = filter.begin(); it != filter.end(); ++it) {
        if (!record.metadata.contains(it.key())) {
            return false;
        }
        if (record.metadata[it.key()] != it.value()) {
            return false;
        }
    }
    return true;
}

nlohmann::json VisionService::handleQuery(const std::string& topic, const nlohmann::json& data) {
    if (topic == "query.vision.face.list") {
        auto filter = data.value("filter", nlohmann::json::object());
        auto limit = data.value("limit", 100);
        auto offset = data.value("offset", 0);

        auto all = face_db_.list();
        std::vector<FaceRecord> filtered;
        for (const auto& record : all) {
            if (matchesFilter(record, filter)) {
                filtered.push_back(record);
            }
        }

        nlohmann::json faces = nlohmann::json::array();
        int total = static_cast<int>(filtered.size());
        for (int i = offset; i < total && static_cast<int>(faces.size()) < limit; ++i) {
            const auto& record = filtered[i];
            faces.push_back({
                {"face_id", record.face_id},
                {"name", record.name},
                {"created_at", record.created_at},
                {"last_seen", record.last_seen},
                {"metadata", record.metadata},
                {"sample_count", record.sample_count}
            });
        }
        return nlohmann::json{{"success", true}, {"total", total}, {"faces", faces}};
    }

    if (topic == "query.vision.config") {
        nlohmann::json cfg = {
            {"vision_enabled", runtime_control_.isEnabled()},
            {"vision_streaming_enabled", runtime_control_.isStreamingEnabled()},
            {"vision_eye_follow", bus_config_.enable_eye_follow},
            {"vision_status_interval_ms", bus_config_.status_interval_ms},
            {"vision_gaze_interval_ms", bus_config_.gaze_publish_interval_ms},
            {"vision_initial_mode", runModeToString(bus_config_.initial_mode)}
        };
        return nlohmann::json{{"success", true}, {"config", cfg}};
    }

    return nlohmann::json{{"success", false}, {"error", "unsupported query"}};
}

void VisionService::startConfigWatcher() {
    if (options_.config_path.empty()) {
        return;
    }
    config_running_.store(true);
    config_thread_ = std::thread(&VisionService::configWatchLoop, this);
}

void VisionService::stopConfigWatcher() {
    if (!config_running_.exchange(false)) {
        return;
    }
    if (config_thread_.joinable()) {
        config_thread_.join();
    }
}

void VisionService::configWatchLoop() {
    namespace fs = std::filesystem;
    while (config_running_.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        if (options_.config_path.empty()) {
            continue;
        }
        std::error_code ec;
        auto time = fs::last_write_time(options_.config_path, ec);
        if (ec) {
            continue;
        }
        auto stamp = time.time_since_epoch().count();
        auto last = config_mtime_.load();
        if (stamp == last) {
            continue;
        }
        config_mtime_.store(stamp);

        if (Settings::load(options_.config_path)) {
            applyRuntimeConfig();
        }
    }
}

int VisionService::run() {
    loadSettings();

    applyRuntimeConfig();
    face_db_.load();

    registerModules();

    if (!startBusBridge()) {
        std::cerr << "❌ VisionBusBridge 启动失败" << std::endl;
    }

    startQueryServer();
    startConfigWatcher();

    if (!InitializeVideoPublisher()) {
        std::cerr << "❌ 视频发布器初始化失败，但继续运行 FaceReco" << std::endl;
    }

    int result = MTCNNDetection(runtime_control_, *bus_bridge_, runtime_metrics_);

    stopConfigWatcher();
    stopQueryServer();
    stopBusBridge();
    ShutdownVideoPublisher();

    return result;
}

}  // namespace doly::vision
