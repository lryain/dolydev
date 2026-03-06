#include "config_utils.h"

#include <fstream>
#include <iostream>
#include <string>

#include "cfg.h"

namespace {
using nlohmann::json;

bool load_config(json &config) {
    std::ifstream config_file(CFG_FILE);
    if (!config_file.is_open()) {
        std::cerr << "[config] Failed to open " << CFG_FILE << " for reading" << std::endl;
        config = json::object();
        return false;
    }

    try {
        config_file >> config;
        return true;
    } catch (const json::parse_error &e) {
        std::cerr << "[config] Failed to parse " << CFG_FILE << ": " << e.what() << std::endl;
        config = json::object();
        return false;
    }
}

bool save_config(const json &config) {
    std::ofstream config_file(CFG_FILE);
    if (!config_file.is_open()) {
        std::cerr << "[config] Failed to open " << CFG_FILE << " for writing" << std::endl;
        return false;
    }

    try {
        config_file << config.dump(4);
        return true;
    } catch (const std::exception &e) {
        std::cerr << "[config] Failed to write to " << CFG_FILE << ": " << e.what() << std::endl;
        return false;
    }
}

int sanitize_playback_sample_rate(int value) {
    if (value < xiaozhi::config::kMinPlaybackSampleRate ||
        value > xiaozhi::config::kMaxPlaybackSampleRate) {
        std::cerr << "[config] playback_sample_rate=" << value
                  << " out of range ("
                  << xiaozhi::config::kMinPlaybackSampleRate << "-"
                  << xiaozhi::config::kMaxPlaybackSampleRate
                  << "), fallback to " << xiaozhi::config::kDefaultPlaybackSampleRate
                  << std::endl;
        return xiaozhi::config::kDefaultPlaybackSampleRate;
    }
    return value;
}

int ensure_playback_sample_rate(json &config, bool &updated) {
    if (!config.is_object()) {
        config = json::object();
        updated = true;
    }

    json &xiaozhi_section = config["xiaozhi"];
    if (!xiaozhi_section.is_object()) {
        xiaozhi_section = json::object();
        updated = true;
    }

    int rate = xiaozhi::config::kDefaultPlaybackSampleRate;
    bool has_value = false;

    if (xiaozhi_section.contains("playback_sample_rate")) {
        const auto &node = xiaozhi_section["playback_sample_rate"];
        try {
            if (node.is_number_integer()) {
                rate = node.get<int>();
                has_value = true;
            } else if (node.is_string()) {
                rate = std::stoi(node.get<std::string>());
                has_value = true;
            }
        } catch (const std::exception &e) {
            std::cerr << "[config] Invalid playback_sample_rate value: " << e.what()
                      << ", fallback to default" << std::endl;
            rate = xiaozhi::config::kDefaultPlaybackSampleRate;
        }
    }

    int sanitized = sanitize_playback_sample_rate(rate);
    if (!has_value || sanitized != rate) {
        updated = true;
    }

    xiaozhi_section["playback_sample_rate"] = sanitized;
    return sanitized;
}
} // namespace

namespace xiaozhi {
namespace config {

int load_playback_sample_rate() {
    json config_json;
    bool read_ok = load_config(config_json);
    bool updated = false;
    int rate = ensure_playback_sample_rate(config_json, updated);

    if (!read_ok || updated) {
        save_config(config_json);
    }

    return rate;
}

} // namespace config
} // namespace xiaozhi
