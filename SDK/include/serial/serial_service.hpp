#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <string>
#include <thread>

namespace doly {
namespace serial {

struct SerialConfig {
    std::string device = "/dev/ttyUSB0"; // default USB0 on Raspberry Pi
    int baud = 115200;
    bool use_simulator = false;
    std::string sim_file;
};

class SerialService {
public:
    using ByteHandler = std::function<void(uint8_t)>;

    SerialService();
    ~SerialService();

    bool init(const SerialConfig& cfg);
    bool start();
    void stop();

    void set_handler(ByteHandler handler);

private:
    void read_loop();

    SerialConfig cfg_;
    std::thread read_thread_;
    std::atomic<bool> running_;
    int fd_ = -1;
    ByteHandler user_handler_;
};

} // namespace serial
} // namespace doly
