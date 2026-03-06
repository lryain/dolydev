#include "msgbus_cpp/subscriber.h"
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <cstring>
#include <iostream>

using namespace msgbus;

Subscriber::Subscriber(const std::string &bind_addr, uint16_t port)
    : bind_addr_(bind_addr), port_(port) {
}

Subscriber::~Subscriber() {
    stop();
}

bool Subscriber::start(MessageHandler handler) {
    if (running_) return false;
    sock_ = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock_ < 0) {
        perror("socket");
        return false;
    }
    struct sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port_);
    if (inet_pton(AF_INET, bind_addr_.c_str(), &addr.sin_addr) <= 0) {
        std::cerr << "invalid bind addr" << std::endl;
        close(sock_);
        sock_ = -1;
        return false;
    }
    if (bind(sock_, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("bind");
        close(sock_);
        sock_ = -1;
        return false;
    }
    running_ = true;
    recv_thread_ = std::thread([this, handler](){
        char buf[65536];
        while (running_) {
            ssize_t n = recvfrom(sock_, buf, sizeof(buf)-1, 0, nullptr, nullptr);
            if (n <= 0) continue;
            std::string s(buf, buf+n);
            Message m = Message::deserialize(s);
            handler(m);
        }
    });
    return true;
}

void Subscriber::stop() {
    if (!running_) return;
    running_ = false;
    if (sock_>=0) close(sock_);
    sock_ = -1;
    if (recv_thread_.joinable()) recv_thread_.join();
}
