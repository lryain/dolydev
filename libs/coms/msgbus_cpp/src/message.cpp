#include "msgbus_cpp/message.h"
#include <sstream>

using namespace msgbus;

std::string Message::serialize() const {
    std::ostringstream ss;
    ss << static_cast<uint16_t>(type) << "|" << source << "|" << data;
    return ss.str();
}

Message Message::deserialize(const std::string &s) {
    Message m;
    size_t p1 = s.find('|');
    if (p1==std::string::npos) return m;
    size_t p2 = s.find('|', p1+1);
    if (p2==std::string::npos) return m;
    uint16_t t = static_cast<uint16_t>(std::stoi(s.substr(0,p1)));
    m.type = static_cast<MessageType>(t);
    m.source = s.substr(p1+1, p2 - (p1+1));
    m.data = s.substr(p2+1);
    return m;
}
