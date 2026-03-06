#include "touch_event_listener.h"

#include <cerrno>
#include <chrono>
#include <cstring>
#include <iostream>
#include <string>
#include <utility>

#include <nlohmann/json.hpp>
#include <zmq.hpp>

TouchEventListener::TouchEventListener()
    : running_(false) {
}

TouchEventListener::~TouchEventListener() {
    stop();
}

bool TouchEventListener::start(TouchCallback single_tap, TouchCallback double_tap) {
    if (running_.load()) {
        std::cerr << "[TouchEventListener] already running" << std::endl;
        return false;
    }

    single_tap_cb_ = std::move(single_tap);
    double_tap_cb_ = std::move(double_tap);
    running_.store(true);
    worker_ = std::thread(&TouchEventListener::run, this);
    return true;
}

void TouchEventListener::stop() {
    running_.store(false);
    if (worker_.joinable()) {
        worker_.join();
    }
}

void TouchEventListener::run() {
    zmq::context_t context(1);
    zmq::socket_t subscriber(context, zmq::socket_type::sub);

    try {
        subscriber.connect("ipc:///tmp/doly_zmq.sock");
    } catch (const zmq::error_t& err) {
        std::cerr << "[TouchEventListener] zmq connect failed: " << err.what() << std::endl;
        return;
    }

    const char topic[] = "io.pca9535.touch.gesture";
    subscriber.setsockopt(ZMQ_SUBSCRIBE, topic, std::strlen(topic));
    const int timeout_ms = 500;
    subscriber.setsockopt(ZMQ_RCVTIMEO, &timeout_ms, sizeof(timeout_ms));

    while (running_.load()) {
        zmq::message_t topic_msg;
        zmq::message_t payload_msg;

        if (!subscriber.recv(topic_msg, zmq::recv_flags::none)) {
            if (running_.load() && zmq_errno() == EAGAIN) {
                continue;
            }
            break;
        }

        if (!subscriber.recv(payload_msg, zmq::recv_flags::none)) {
            continue;
        }

        std::string payload(static_cast<char*>(payload_msg.data()), payload_msg.size());

        try {
            auto data = nlohmann::json::parse(payload);
            std::string gesture = data.value("gesture", "");

            if (gesture == "SINGLE_TAP" && single_tap_cb_) {
                single_tap_cb_();
            } else if (gesture == "DOUBLE_TAP" && double_tap_cb_) {
                double_tap_cb_();
            }
        } catch (const nlohmann::json::parse_error& err) {
            std::cerr << "[TouchEventListener] invalid JSON: " << err.what() << std::endl;
            continue;
        }
    }

    subscriber.close();
}
