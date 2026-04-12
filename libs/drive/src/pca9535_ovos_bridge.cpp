/**
 * @file pca9535_ovos_bridge.cpp
 * @brief PCA9535 OVOS Control Bridge Implementation
 * 
 * Phase 4: Uses independent ZeroMQ SUB socket for control commands
 

## 许可
GNU General Public License v3.0

## 作者
Kevin.Liu @ Make&Share
QQ: 47129927@qq.com

*/
#include "doly/pca9535_ovos_bridge.hpp"
#include <iostream>
#include <cstring>

namespace doly {
namespace extio {

using json = nlohmann::json;

Pca9535OvosBridge::Pca9535OvosBridge(Pca9535Service& service)
    : service_(service) {}

Pca9535OvosBridge::~Pca9535OvosBridge() {
    stop();
}

bool Pca9535OvosBridge::start() {
    if (running_.load()) return true;

    running_.store(true);
    
    // Start independent subscriber thread
    thread_ = std::thread(&Pca9535OvosBridge::subscriber_thread, this);
    
    std::cout << "[Pca9535OvosBridge] Started (subscribing to io.pca9535.control.* on independent SUB socket)" << std::endl;
    return true;
}

void Pca9535OvosBridge::stop() {
    if (!running_.load()) return;

    running_.store(false);
    
    if (thread_.joinable()) {
        thread_.join();
    }
    
    std::cout << "[Pca9535OvosBridge] Stopped" << std::endl;
}

void Pca9535OvosBridge::subscriber_thread() {
    // Create independent ZeroMQ SUB socket
    zmq_context_ = zmq_ctx_new();
    zmq_socket_ = zmq_socket(zmq_context_, ZMQ_SUB);
    
    // Connect to drive_service's PUB socket
    zmq_connect(zmq_socket_, "ipc:///tmp/doly_zmq.sock");
    
    // Subscribe to io.pca9535.control.* topic
    const char* topic_filter = "io.pca9535.control.";
    zmq_setsockopt(zmq_socket_, ZMQ_SUBSCRIBE, topic_filter, strlen(topic_filter));
    
    // Set receive timeout (1s) to allow responding to stop()
    int timeout = 1000;
    zmq_setsockopt(zmq_socket_, ZMQ_RCVTIMEO, &timeout, sizeof(timeout));
    
    std::cout << "[Pca9535OvosBridge] Subscriber thread started, listening for control commands..." << std::endl;
    
    char topic_buf[256];
    char payload_buf[2048];
    
    while (running_.load()) {
        // Receive topic
        int topic_size = zmq_recv(zmq_socket_, topic_buf, sizeof(topic_buf) - 1, 0);
        if (topic_size < 0) {
            if (errno == EAGAIN) {
                continue;  // Timeout, continue waiting
            }
            std::cerr << "[Pca9535OvosBridge] zmq_recv topic error: " << zmq_strerror(errno) << std::endl;
            break;
        }
        topic_buf[topic_size] = '\0';
        
        // Receive payload
        int payload_size = zmq_recv(zmq_socket_, payload_buf, sizeof(payload_buf) - 1, 0);
        if (payload_size < 0) {
            std::cerr << "[Pca9535OvosBridge] zmq_recv payload error" << std::endl;
            continue;
        }
        payload_buf[payload_size] = '\0';
        
        std::cout << "[Pca9535OvosBridge] Received: topic=" << topic_buf 
                  << ", payload_size=" << payload_size << std::endl;
        
        // Parse JSON payload
        try {
            json payload = json::parse(payload_buf);
            
            // Extract subtopic (remove "io.pca9535.control." prefix)
            const char* prefix = "io.pca9535.control.";
            std::string subtopic;
            if (strncmp(topic_buf, prefix, strlen(prefix)) == 0) {
                subtopic = topic_buf + strlen(prefix);
            }
            
            // Handle control command
            bool handled = handle_control_topic(subtopic, payload);
            
            std::cout << "[Pca9535OvosBridge] Command: " << subtopic << " => " 
                      << (handled ? "OK" : "FAILED") << std::endl;
            
            // (Optional: Send ACK to OVOS - disabled for now)
            
        } catch (const std::exception& e) {
            std::cerr << "[Pca9535OvosBridge] Failed to parse payload: " << e.what() << std::endl;
        }
    }
    
    // Cleanup ZeroMQ resources
    zmq_close(zmq_socket_);
    zmq_ctx_destroy(zmq_context_);
    
    std::cout << "[Pca9535OvosBridge] Subscriber thread stopped" << std::endl;
}

bool Pca9535OvosBridge::handle_control_topic(const std::string& topic, const json& payload) {
    // Read value/enable field from payload (compatible with old and new formats)
    bool value = payload.value("value", payload.value("enable", false));

    // Servo control (compatible with old and new formats)
    if (topic == "servo_left" || topic == "enable_servo_left") {
        return service_.enable_servo_left(value);
    } else if (topic == "servo_right" || topic == "enable_servo_right") {
        return service_.enable_servo_right(value);
    }
    // TOF enable
    else if (topic == "tof" || topic == "enable_tof") {
        return service_.enable_tof(value);
    }
    // Cliff enable
    else if (topic == "cliff" || topic == "enable_cliff") {
        return service_.enable_cliff(value);
    }
    // (Generic GPIO operations removed - not supported by current service API)
    // Unknown command
    else {
        std::cerr << "[Pca9535OvosBridge] Unknown control topic: " << topic << std::endl;
        return false;
    }
}

} // namespace extio
} // namespace doly
