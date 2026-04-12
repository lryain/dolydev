#include "serial/serial_service.hpp"

#include <nlohmann/json.hpp>
#include <yaml-cpp/yaml.h>
#include <iostream>
#include <signal.h>
#include <thread>
#include <chrono>
#include <string>
#include <sstream>
#include <unordered_map>
#include <cstdlib>
#include <zmq.hpp>
#include <memory>

using namespace doly;
using namespace doly::serial;
using json = nlohmann::json;

static inline std::vector<uint8_t> json_to_bytes(const json& j) {
    auto s = j.dump();
    std::vector<uint8_t> v(s.size());
    memcpy(v.data(), s.data(), s.size());
    return v;
}

static volatile bool g_running = true;

static void handle_sigint(int) {
    g_running = false;
}

int main(int argc, char** argv) {
    SerialConfig cfg;
    // default device is /dev/ttyUSB0
    std::string dev = "/dev/ttyUSB0";
    int baud = 115200;
    bool simulate = false;
    std::string simfile;
    std::string config_file = "/home/pi/dolydev/config/serial.yaml";
    bool dev_set_by_cli = false;
    bool baud_set_by_cli = false;

    // ZMQ configuration
    std::string zmq_endpoint = "ipc:///tmp/doly_serial_pub.sock";
    std::string zmq_mode = "server";

    // ZMQ objects
    zmq::context_t zmq_context(1);
    std::unique_ptr<zmq::socket_t> zmq_socket;

    for (int i = 1; i < argc; ++i) {
        std::string arg(argv[i]);
    if (arg == "--dev" && i + 1 < argc) { dev = argv[++i]; dev_set_by_cli = true; }
    else if (arg == "--baud" && i + 1 < argc) { baud = std::stoi(argv[++i]); baud_set_by_cli = true; }
        else if (arg == "--simulate" && i + 1 < argc) { simulate = true; simfile = argv[++i]; }
    else if (arg == "--config" && i + 1 < argc) { config_file = argv[++i]; }
    else if (arg == "--no-config") { config_file.clear(); }
    else if (arg == "--help") {
            std::cout << "Usage: serial_service [--dev /dev/ttyUSB0] [--baud 115200] [--simulate simulate_data.bin]" << std::endl;
            return 0;
        }
    }

    cfg.device = dev;
    cfg.baud = baud;
    cfg.use_simulator = simulate;
    cfg.sim_file = simfile;

    // mapping codes to topics (default mapping - can be overridden by YAML config)
    std::unordered_map<uint8_t, std::string> mapping;

    // Load YAML config if available
    if (!config_file.empty()) {
        try {
            YAML::Node conf = YAML::LoadFile(config_file);
            if (conf) {
                // device
                if (conf["device"] && !dev_set_by_cli) {
                    cfg.device = conf["device"].as<std::string>();
                    dev = cfg.device;
                }
                if (conf["baud"] && !baud_set_by_cli) {
                    cfg.baud = conf["baud"].as<int>();
                    baud = cfg.baud;
                }
                // zmq config
                if (conf["zmq"] && conf["zmq"].IsMap()) {
                    auto zmq_conf = conf["zmq"];
                    if (zmq_conf["pub_endpoint"]) {
                        zmq_endpoint = zmq_conf["pub_endpoint"].as<std::string>();
                        std::cout << "[SerialService] ZMQ endpoint: " << zmq_endpoint << std::endl;
                    }
                    if (zmq_conf["mode"]) {
                        zmq_mode = zmq_conf["mode"].as<std::string>();
                        std::cout << "[SerialService] ZMQ mode: " << zmq_mode << std::endl;
                    }
                }
                // mappings
                if (conf["mappings"] && conf["mappings"].IsMap()) {
                    mapping.clear();
                    for (auto it : conf["mappings"]) {
                        // parse key as hex or int
                        uint8_t key = 0;
                        try {
                            auto keynode = it.first;
                            std::string keystr = keynode.as<std::string>();
                            // allow 0xNN or decimal
                            unsigned long k = std::stoul(keystr, nullptr, 0);
                            key = static_cast<uint8_t>(k & 0xff);
                        } catch (const YAML::BadConversion& e) {
                            try {
                                int k = it.first.as<int>();
                                key = static_cast<uint8_t>(k & 0xff);
                            } catch (...) {
                                std::cerr << "[SerialService] Invalid mapping key in config: skipping" << std::endl;
                                continue;
                            }
                        } catch (...) {
                            std::cerr << "[SerialService] Invalid mapping key in config: skipping" << std::endl;
                            continue;
                        }
                        try {
                            std::string topic = it.second.as<std::string>();
                            mapping[key] = topic;
                        } catch (...) {
                            std::cerr << "[SerialService] Invalid mapping value in config for key" << (int)key << std::endl;
                            continue;
                        }
                    }
                }
            }
        } catch (const std::exception& e) {
            std::cerr << "[SerialService] Failed to load config file " << config_file << ": " << e.what() << std::endl;
        }
    }

    // Initialize ZMQ
    try {
        zmq_socket = std::make_unique<zmq::socket_t>(zmq_context, ZMQ_PUB);
        
        // Set socket options
        int linger = 0;
        zmq_socket->setsockopt(ZMQ_LINGER, &linger, sizeof(linger));
        
        // Bind or connect based on mode
        if (zmq_mode == "server" || zmq_mode == "bind") {
            zmq_socket->bind(zmq_endpoint);
            std::cout << "[SerialService] ZMQ PUB socket bound to: " << zmq_endpoint << std::endl;
        } else {
            zmq_socket->connect(zmq_endpoint);
            std::cout << "[SerialService] ZMQ PUB socket connected to: " << zmq_endpoint << std::endl;
        }
    } catch (const zmq::error_t& e) {
        std::cerr << "[SerialService] Failed to initialize ZMQ: " << e.what() << std::endl;
        return 1;
    }

    SerialService service;
    if (!service.init(cfg)) {
        std::cerr << "[SerialService] init failed" << std::endl;
        return 2;
    }

    service.set_handler([&](uint8_t b) {
        auto it = mapping.find(b);
        if (it == mapping.end()) {
            std::cerr << "[SerialService] Unknown code: 0x" << std::hex << (int)b << std::dec << std::endl;
            return;
        }
        const std::string& topic = it->second;
        json payload = {
            {"ts_us", static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count())},
            {"code", (int)b}
        };
        // create hex string
        {
            std::stringstream ss;
            ss << std::hex << std::uppercase << (int)b;
            payload["hex"] = std::string("0x") + ss.str();
        }
        
        // Send via ZMQ
        try {
            std::string payload_str = payload.dump();
            zmq::message_t topic_msg(topic.data(), topic.size());
            zmq::message_t payload_msg(payload_str.data(), payload_str.size());
            
            zmq_socket->send(topic_msg, ZMQ_SNDMORE);
            zmq_socket->send(payload_msg);
            
            std::cout << "[SerialService] Published event: " << topic << " payload=" << payload_str << std::endl;
        } catch (const zmq::error_t& e) {
            std::cerr << "[SerialService] Failed to publish: " << topic << " error: " << e.what() << std::endl;
        }
    });

    if (!service.start()) {
        std::cerr << "[SerialService] start failed" << std::endl;
        return 3;
    }

    std::cout << "SerialService running. device=" << cfg.device << " baud=" << cfg.baud << std::endl;
    signal(SIGINT, handle_sigint);
    while (g_running) {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }

    service.stop();

    std::cout << "SerialService stopped" << std::endl;
    return 0;
}
