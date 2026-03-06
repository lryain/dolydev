#include "msgbus_cpp/publisher.h"
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <netdb.h>
#include <cstring>
#include <iostream>

using namespace msgbus;

Publisher::Publisher(const std::string &host, uint16_t port)
    : host_(host), port_(port) {
    sock_ = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock_ < 0) {
        perror("socket");
        sock_ = -1;
    }
}

Publisher::~Publisher() {
    if (sock_ >= 0) close(sock_);
}

bool Publisher::publish(const Message &m) {
    if (sock_ < 0) return false;
    struct sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port_);
    if (inet_pton(AF_INET, host_.c_str(), &addr.sin_addr) <= 0) {
        // try resolve
        struct hostent *he = gethostbyname(host_.c_str());
        if (!he) return false;
        addr.sin_addr = *(struct in_addr*)he->h_addr;
    }
    std::string out = m.serialize();
    ssize_t sent = sendto(sock_, out.data(), out.size(), 0,
                          (struct sockaddr*)&addr, sizeof(addr));
    return sent == (ssize_t)out.size();
}
