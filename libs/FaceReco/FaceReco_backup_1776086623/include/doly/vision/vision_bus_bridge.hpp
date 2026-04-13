#pragma once

#include "doly/vision/runtime_control.hpp"
#include "doly/vision/module_types.hpp"
#include "doly/vision/face_database.hpp"
#include <nlohmann/json.hpp>
#include <zmq.hpp>
#include <atomic>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <optional>
#include <memory>

namespace doly::vision {

struct RuntimeMetrics {
    std::atomic<double> fps{0.0};
    std::atomic<int> active_tracks{0};
    std::atomic<int> recognized_faces{0};
};

class VisionBusBridge {
public:
    struct Config {
        std::string pub_endpoint{"ipc:///tmp/doly_events.sock"};
        std::string sub_endpoint{"ipc:///tmp/doly_events.sock"};
        std::string source_id{"vision_service"};
        int status_interval_ms{1000};
        bool enable_eye_follow{true};
        double eye_follow_smoothing{0.12};
        int gaze_publish_interval_ms{50};
        RunMode initial_mode{RunMode::FULL};
    bool auto_start{true};
    int mode_timeout_seconds{30};
    bool enable_mode_timeout{true};
        
        // 人脸跟踪配置
        std::string tracking_mode{"gaze_only"};  // gaze_only, motor_only, both, disabled
        bool gaze_enabled{true};
        float gaze_smoothing{0.12f};
        float gaze_dead_zone{0.08f};
        bool motor_enabled{false};
        float motor_trigger_threshold{0.3f};
        int motor_speed{30};
    };

    VisionBusBridge(const Config& config,
                    RuntimeControl& control,
                    RuntimeMetrics& metrics,
                    std::function<bool(RunMode)> mode_handler = {},
                    std::function<nlohmann::json(const std::string&, const nlohmann::json&)> face_handler = {});
    ~VisionBusBridge();

    bool start();
    void stop();

    void publishEvent(const std::string& topic, const nlohmann::json& data);
    void publishStatusSnapshot();

    void publishFaceSnapshot(const nlohmann::json& payload);
    void publishRecognitionEvent(const nlohmann::json& payload);
    void publishFaceLostEvent(const nlohmann::json& payload);
    void publishCaptureStarted(const nlohmann::json& payload);
    void publishCaptureComplete(const nlohmann::json& payload);
    void publishCaptureResult(const nlohmann::json& payload);

    void updateLatestFace(const nlohmann::json& primary_face);
    void updateRuntimeConfig(const Config& config);
    void applyMode(RunMode mode, bool mark_mode_received);
    bool isFaceOpsAllowed() const;
    bool isRecognitionAllowed() const;
    bool hasModeSignal() const;
    
    // 人脸数据库操作（新增）
    void setFaceDatabase(std::shared_ptr<FaceDatabase> face_db);

private:
    void commandThread();
    void statusThread();
    void timeoutCheckThread();  // 🆕 超时检查线程
    void ensurePublisher();
    void sendEyeGazeCommand(const nlohmann::json& primary_face);
    void sendMotorTrackingCommand(const nlohmann::json& primary_face);  // 📌 Task 2-3: 电机跟踪
    
    // 人脸数据库命令处理（新增）
    void handleFaceRegisterCommand(const nlohmann::json& payload);
    void handleFaceUpdateCommand(const nlohmann::json& payload);
    void handleFaceDeleteCommand(const nlohmann::json& payload);
    void handleFaceQueryCommand(const nlohmann::json& payload);

    nlohmann::json wrapEnvelope(const nlohmann::json& data) const;

    Config config_;
    RuntimeControl& control_;
    RuntimeMetrics& metrics_;
    std::function<bool(RunMode)> mode_handler_{};
    std::function<nlohmann::json(const std::string&, const nlohmann::json&)> face_handler_{};
    std::atomic<RunMode> current_mode_{RunMode::FULL};
    std::atomic<bool> enable_eye_follow_{true};
    std::atomic<int> status_interval_ms_{1000};
    std::atomic<int> gaze_publish_interval_ms_{50};
    std::atomic<bool> mode_received_{false};

    std::unique_ptr<zmq::context_t> context_;
    std::unique_ptr<zmq::socket_t> pub_socket_;

    std::thread sub_thread_;
    std::thread status_thread_;
    std::thread timeout_thread_;  // 🆕 超时检查线程
    std::atomic<bool> running_{false};

    std::mutex pub_mutex_;

    std::mutex latest_face_mutex_;
    std::optional<nlohmann::json> latest_primary_face_;
    std::chrono::steady_clock::time_point last_gaze_publish_{std::chrono::steady_clock::now()};
    
    // 🆕 模式超时管理
    std::chrono::steady_clock::time_point mode_start_time_{std::chrono::steady_clock::now()};
    int mode_timeout_seconds_{30};
    bool enable_mode_timeout_{true};
    
    // 人脸数据库（新增）
    std::shared_ptr<FaceDatabase> face_db_;
};

}  // namespace doly::vision
