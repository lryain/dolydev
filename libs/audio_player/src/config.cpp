#include "audio_player/config.hpp"

#include <yaml-cpp/yaml.h>

#include <iostream>

namespace doly {
namespace audio {

namespace {

template <typename T>
T read_or(const YAML::Node& node, const std::string& key, const T& fallback) {
    if (!node || !node[key]) {
        return fallback;
    }
    try {
        return node[key].as<T>();
    } catch (const YAML::Exception& e) {
        std::cerr << "[AudioPlayerConfig] bad key '" << key << "': " << e.what() << std::endl;
        return fallback;
    }
}

} // namespace

AudioPlayerConfig load_config(const std::string& path) {
    AudioPlayerConfig cfg;

    YAML::Node root;
    try {
        root = YAML::LoadFile(path);
    } catch (const std::exception& e) {
        throw std::runtime_error(std::string("Failed to load audio player config: ") + e.what());
    }

    if (auto device = root["device"]) {
        cfg.device = read_or<std::string>(root, "device", cfg.device);
    }

    if (auto endpoint = root["cmd_endpoint"]) {
        cfg.cmd_endpoint = read_or<std::string>(root, "cmd_endpoint", cfg.cmd_endpoint);
    }

    if (auto status = root["status_topic"]) {
        cfg.status_topic = read_or<std::string>(root, "status_topic", cfg.status_topic);
    }

    if (auto endpoint = root["status_endpoint"]) {
        cfg.status_endpoint = read_or<std::string>(root, "status_endpoint", cfg.status_endpoint);
    }

    if (auto stream_ep = root["stream_endpoint"]) {
        cfg.stream_endpoint = read_or<std::string>(root, "stream_endpoint", cfg.stream_endpoint);
    }

    cfg.max_concurrent_sounds = read_or<uint32_t>(root, "max_concurrent_sounds", cfg.max_concurrent_sounds);
    cfg.stream_buffer_ms = read_or<uint32_t>(root, "stream_buffer_ms", cfg.stream_buffer_ms);
    cfg.ducking_level = read_or<float>(root, "ducking_level", cfg.ducking_level);

    cfg.path_prefix = read_or<std::string>(root, "path_prefix", cfg.path_prefix);
    if (!cfg.path_prefix.empty() && cfg.path_prefix.back() != '/') {
        cfg.path_prefix.push_back('/');
    }

    if (auto aliases = root["alias_paths"]) {
        for (const auto& kv : aliases) {
            if (kv.first && kv.second) {
                cfg.alias_paths[kv.first.as<std::string>()] = kv.second.as<std::string>();
            }
        }
    }

    if (auto priorities = root["priority_map"]) {
        for (const auto& kv : priorities) {
            if (kv.first && kv.second) {
                cfg.priority_map[kv.first.as<std::string>()] = kv.second.as<int>();
            }
        }
    }

    if (auto preload = root["preload_aliases"]) {
        for (const auto& alias : preload) {
            cfg.preload_aliases.push_back(alias.as<std::string>());
        }
    }

    return cfg;
}

} // namespace audio
} // namespace doly
