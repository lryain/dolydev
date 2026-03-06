#pragma once

#include "msgbus_cpp/message.h"
#include <functional>
#include <string>
#include <thread>

namespace msgbus {

using MessageHandler = std::function<void(const Message&)>;

class Subscriber {
public:
    Subscriber(const std::string &bind_addr = "0.0.0.0", uint16_t port = 5555);
    ~Subscriber();

    // start listening in background
    bool start(MessageHandler handler);
    void stop();

private:
    int sock_ = -1;
    std::string bind_addr_;
    uint16_t port_ = 5555;
    std::thread recv_thread_;
    bool running_ = false;
};

} // namespace msgbus
