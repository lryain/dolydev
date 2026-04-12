#include "serial/serial_service.hpp"

#include <fcntl.h>
#include <unistd.h>
#include <termios.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <chrono>
#include <cstring>
#include <iostream>

namespace doly {
namespace serial {

SerialService::SerialService() : running_(false), fd_(-1) {}

SerialService::~SerialService() {
    stop();
}

static speed_t baud_to_speed(int baud) {
    switch (baud) {
        case 9600: return B9600;
        case 19200: return B19200;
        case 38400: return B38400;
        case 57600: return B57600;
        case 115200: return B115200;
        case 230400: return B230400;
        default: return B115200;
    }
}

bool SerialService::init(const SerialConfig& cfg) {
    cfg_ = cfg;
    return true;
}

bool SerialService::start() {
    if (running_.load()) {
        std::cerr << "[SerialService] Already running" << std::endl;
        return false;
    }

    if (cfg_.use_simulator) {
        // For simulator, open as normal file
        fd_ = open(cfg_.sim_file.c_str(), O_RDONLY | O_NONBLOCK);
        if (fd_ < 0) {
            std::cerr << "[SerialService] Failed to open simulator file: " << cfg_.sim_file << " err=" << strerror(errno) << std::endl;
            return false;
        }
    } else {
        // Open serial device
        fd_ = open(cfg_.device.c_str(), O_RDONLY | O_NOCTTY | O_NONBLOCK);
        if (fd_ < 0) {
            std::cerr << "[SerialService] Failed to open device: " << cfg_.device << " err=" << strerror(errno) << std::endl;
            return false;
        }
        // Configure port
        struct termios tty;
        if (tcgetattr(fd_, &tty) != 0) {
            std::cerr << "[SerialService] tcgetattr failed: " << strerror(errno) << std::endl;
            close(fd_);
            fd_ = -1;
            return false;
        }

        cfmakeraw(&tty);
        speed_t speed = baud_to_speed(cfg_.baud);
        cfsetospeed(&tty, speed);
        cfsetispeed(&tty, speed);

        // 8N1
        tty.c_cflag &= ~PARENB;
        tty.c_cflag &= ~CSTOPB;
        tty.c_cflag &= ~CSIZE;
        tty.c_cflag |= CS8;
        // disable flow control
        tty.c_cflag &= ~CRTSCTS;
        tty.c_cflag |= CREAD | CLOCAL;

        // set timeouts to block read less than 1s
        tty.c_cc[VMIN] = 1;
        tty.c_cc[VTIME] = 1;

        if (tcsetattr(fd_, TCSANOW, &tty) != 0) {
            std::cerr << "[SerialService] tcsetattr failed: " << strerror(errno) << std::endl;
            close(fd_);
            fd_ = -1;
            return false;
        }
    }

    running_.store(true);
    read_thread_ = std::thread(&SerialService::read_loop, this);
    return true;
}

void SerialService::stop() {
    running_.store(false);
    if (read_thread_.joinable()) {
        read_thread_.join();
    }
    if (fd_ >= 0) {
        close(fd_);
        fd_ = -1;
    }
}

void SerialService::set_handler(ByteHandler handler) {
    user_handler_ = handler;
}

void SerialService::read_loop() {
    const size_t buf_size = 256;
    uint8_t buf[buf_size];

    while (running_.load()) {
        if (fd_ < 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            continue;
        }
        ssize_t n = read(fd_, buf, buf_size);
        if (n < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                continue;
            } else {
                std::cerr << "[SerialService] read error: " << strerror(errno) << std::endl;
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                continue;
            }
        }
        if (n == 0) {
            // EOF for file/simulator
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }
        for (ssize_t i = 0; i < n; ++i) {
            uint8_t b = buf[i];
            if (user_handler_) {
                user_handler_(b);
            }
        }
    }
}

} // namespace serial
} // namespace doly
