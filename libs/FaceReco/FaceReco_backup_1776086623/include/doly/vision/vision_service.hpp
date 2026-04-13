#pragma once

#include "doly/vision/module_manager.hpp"
#include "doly/vision/runtime_control.hpp"
#include "doly/vision/vision_bus_bridge.hpp"
#include "doly/vision/face_database.hpp"

#include <string>
#include <thread>
#include <atomic>
#include <zmq.hpp>

namespace doly::vision {

class VisionService {
public:
    struct Options {
        std::string config_path;
    };

    explicit VisionService(Options options);

    VisionService(const VisionService&) = delete;
    VisionService& operator=(const VisionService&) = delete;

    int run();

private:
    bool loadSettings();
    void registerModules();
    bool startBusBridge();
    void stopBusBridge();
    void startConfigWatcher();
    void stopConfigWatcher();
    void configWatchLoop();
    void applyRuntimeConfig();
    nlohmann::json handleFaceCommand(const std::string& topic, const nlohmann::json& data);
    nlohmann::json handleQuery(const std::string& topic, const nlohmann::json& data);
    void startQueryServer();
    void stopQueryServer();
    void queryLoop();

    Options options_;
    RuntimeControl runtime_control_;
    RuntimeMetrics runtime_metrics_;
    ModuleManager module_manager_;
    FaceDatabase face_db_;
    VisionBusBridge::Config bus_config_;
    std::unique_ptr<VisionBusBridge> bus_bridge_;
    std::thread config_thread_;
    std::atomic<bool> config_running_{false};
    std::atomic<long long> config_mtime_{0};

    std::unique_ptr<zmq::context_t> query_context_;
    std::unique_ptr<zmq::socket_t> query_socket_;
    std::thread query_thread_;
    std::atomic<bool> query_running_{false};
    std::string query_endpoint_{"ipc:///tmp/doly_vision_query.sock"};
};

}  // namespace doly::vision
