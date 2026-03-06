#pragma once

#include "msgbus_cpp/message.h"
#include <string>

namespace msgbus {

class Publisher {
public:
    Publisher(const std::string &host = "127.0.0.1", uint16_t port = 5555);
    ~Publisher();

    bool publish(const Message &m);

private:
    int sock_ = -1;
    std::string host_;
    uint16_t port_ = 5555;
};

} // namespace msgbus
