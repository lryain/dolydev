#include "control_center_utils.h"
#include <fstream>
#include <iostream>
#include "cfg.h"
#include "uuid.h"
#include "websocket_client.h"
#include "config_utils.h"

using json = nlohmann::json;

std::string read_uuid_from_config() {
    std::ifstream config_file(CFG_FILE);
    if (!config_file.is_open()) {
        std::cerr << "Failed to open " CFG_FILE " for reading" << std::endl;
        return "";
    }

    try {
        json config_json;
        config_file >> config_json;
        config_file.close();

        if (config_json.contains("uuid")) {
            return config_json["uuid"].get<std::string>();
        }
    } catch (const nlohmann::json::parse_error& e) {
        std::cerr << "Failed to parse " CFG_FILE ": " << e.what() << std::endl;
    }

    return "";
}

bool write_uuid_to_config(const std::string& uuid) {
    std::ofstream config_file(CFG_FILE);
    if (!config_file.is_open()) {
        std::cerr << "Failed to open " CFG_FILE " for writing" << std::endl;
        return false;
    }

    try {
        json config_json;
        config_json["uuid"] = uuid;
        config_file << config_json.dump(4);
        config_file.close();
        return true;
    } catch (const nlohmann::json::parse_error& e) {
        std::cerr << "Failed to write to " CFG_FILE ": " << e.what() << std::endl;
    }

    return false;
}

bool is_webtts_config_exists() {
    std::ifstream config_file(CFG_FILE);
    if (!config_file.is_open()) {
        return false;
    }

    try {
        json config_json;
        config_file >> config_json;
        return config_json.contains("webtts");
    } catch (const nlohmann::json::parse_error& e) {
        std::cerr << "Failed to parse " CFG_FILE ": " << e.what() << std::endl;
        return false;
    }
}

bool write_webtts_to_config(const WebttsConfig& config) {
    json config_json;
    {
        std::ifstream existing_file(CFG_FILE);
        if (existing_file.is_open()) {
            try {
                existing_file >> config_json;
            } catch (const nlohmann::json::parse_error& e) {
                std::cerr << "Failed to parse existing " CFG_FILE ": " << e.what() << std::endl;
            }
        }
    }

    if (!config_json.contains("webtts")) {
        config_json["webtts"] = json::object();
    }

    auto& webtts = config_json["webtts"];
    webtts["app_id"] = config.app_id;
    webtts["api_key"] = config.api_key;
    webtts["api_secret"] = config.api_secret;
    webtts["sample_rate"] = config.sample_rate;
    webtts["voice_name"] = config.voice_name;
    webtts["speed"] = config.speed;

    std::ofstream config_file(CFG_FILE);
    if (!config_file.is_open()) {
        std::cerr << "Failed to open " CFG_FILE " for writing" << std::endl;
        return false;
    }

    try {
        config_file << config_json.dump(4);
        config_file.close();
        return true;
    } catch (const std::exception& e) {
        std::cerr << "Failed to write to " CFG_FILE ": " << e.what() << std::endl;
    }

    return false;
}

bool read_webtts_from_config(WebttsConfig& config) {
    std::ifstream config_file(CFG_FILE);
    if (!config_file.is_open()) {
        std::cerr << "Failed to open " CFG_FILE " for reading" << std::endl;
        return false;
    }

    try {
        json config_json;
        config_file >> config_json;
        config_file.close();

        if (config_json.contains("webtts") && config_json["webtts"].is_object()) {
            const auto& webtts = config_json["webtts"];
            config.app_id = webtts.value("app_id", "");
            config.api_key = webtts.value("api_key", "");
            config.api_secret = webtts.value("api_secret", "");
            config.sample_rate = webtts.value("sample_rate", 16000);
            config.voice_name = webtts.value("voice_name", "xiaoyan");
            config.speed = webtts.value("speed", 50);
            return true;
        } else {
            std::cerr << "Missing 'webtts' object in " CFG_FILE << std::endl;
        }
    } catch (const nlohmann::json::parse_error& e) {
        std::cerr << "Failed to parse " CFG_FILE ": " << e.what() << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "Error reading " CFG_FILE ": " << e.what() << std::endl;
    }

    return false;
}

bool is_xiaozhi_config_exists() {
    std::ifstream config_file(CFG_FILE);
    if (!config_file.is_open()) {
        return false;
    }

    try {
        json config_json;
        config_file >> config_json;
        return config_json.contains("xiaozhi");
    } catch (const nlohmann::json::parse_error& e) {
        std::cerr << "Failed to parse " CFG_FILE ": " << e.what() << std::endl;
        return false;
    }
}

bool write_xiaozhi_to_config(const std::string& ota_url, const std::string& ws_addr, int playback_sample_rate) {
    json config_json;
    {
        std::ifstream existing_file(CFG_FILE);
        if (existing_file.is_open()) {
            try {
                existing_file >> config_json;
            } catch (const nlohmann::json::parse_error& e) {
                std::cerr << "Failed to parse existing " CFG_FILE ": " << e.what() << std::endl;
            }
        }
    }

    if (!config_json.contains("xiaozhi")) {
        config_json["xiaozhi"] = json::object();
    }

    auto& xiaozhi = config_json["xiaozhi"];
    xiaozhi["xiaozhi_ota_url"] = ota_url;
    xiaozhi["xiaozhi_ws_addr"] = ws_addr;
    if (playback_sample_rate > 0) {
        int sanitized = playback_sample_rate;
        if (sanitized < xiaozhi::config::kMinPlaybackSampleRate ||
            sanitized > xiaozhi::config::kMaxPlaybackSampleRate) {
            sanitized = xiaozhi::config::kDefaultPlaybackSampleRate;
        }
        xiaozhi["playback_sample_rate"] = sanitized;
    }

    std::ofstream config_file(CFG_FILE);
    if (!config_file.is_open()) {
        std::cerr << "Failed to open " CFG_FILE " for writing" << std::endl;
        return false;
    }

    try {
        config_file << config_json.dump(4);
        config_file.close();
        return true;
    } catch (const std::exception& e) {
        std::cerr << "Failed to write to " CFG_FILE ": " << e.what() << std::endl;
    }

    return false;
}

bool read_xiaozhi_from_config(std::string& ota_url, std::string& ws_addr) {
    std::ifstream config_file(CFG_FILE);
    if (!config_file.is_open()) {
        std::cerr << "Failed to open " CFG_FILE " for reading" << std::endl;
        return false;
    }

    try {
        json config_json;
        config_file >> config_json;
        config_file.close();

        if (config_json.contains("xiaozhi") && config_json["xiaozhi"].is_object()) {
            const auto& xiaozhi = config_json["xiaozhi"];
            ota_url = xiaozhi.value("xiaozhi_ota_url", "");
            ws_addr = xiaozhi.value("xiaozhi_ws_addr", "");
            return true;
        } else {
            std::cerr << "Missing 'xiaozhi' object in " CFG_FILE << std::endl;
        }
    } catch (const nlohmann::json::parse_error& e) {
        std::cerr << "Failed to parse " CFG_FILE ": " << e.what() << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "Error reading " CFG_FILE ": " << e.what() << std::endl;
    }

    return false;
}

void create_default_xiaozhi_config(std::string &ota_url, std::string &ws_addr)
{
    ota_url = "https://api.tenclass.net/xiaozhi/ota/";
    ws_addr = "wss://api.tenclass.net:443/xiaozhi/v1/";
}

// generate_uuid is provided by uuid.cc

void parseWebSocketUrl(const std::string& url, websocket_data_t& data) {
    size_t protocolPos = url.find("://");
    if (protocolPos != std::string::npos) {
        data.protocol = url.substr(0, protocolPos + 3);
        size_t remainingPos = protocolPos + 3;

        size_t hostEndPos = url.find(':', remainingPos);
        size_t pathStartPos = url.find('/', remainingPos);

        if (hostEndPos != std::string::npos && hostEndPos < pathStartPos) {
            data.hostname = url.substr(remainingPos, hostEndPos - remainingPos);
            size_t portEndPos = url.find('/', hostEndPos);
            if (portEndPos != std::string::npos) {
                data.port = url.substr(hostEndPos + 1, portEndPos - hostEndPos - 1);
                data.path = url.substr(portEndPos);
            } else {
                data.port = url.substr(hostEndPos + 1);
                data.path = "/";
            }
        } else {
            data.hostname = url.substr(remainingPos, pathStartPos - remainingPos);
            data.port = (data.protocol == "wss://") ? "443" : "80";
            if (pathStartPos != std::string::npos) {
                data.path = url.substr(pathStartPos);
            } else {
                data.path = "/";
            }
        }
    }
}

void websocket_send_abort(const std::string& session_id) {
    json j;
    j["session_id"] = session_id;
    j["type"] = "abort";
    j["reason"] = "wake_word_detected";
    std::string abortString = j.dump();
    try {
        websocket_send_text(abortString.data(), abortString.size());
    } catch (const std::exception& e) {
        std::cout << "Error sending message: " << e.what() << std::endl;
    }
}

void websocket_send_listen_start(const std::string& session_id, int mode) {
    std::string startString = "{\"session_id\":\"" + session_id + "\"";
    startString += ",\"type\":\"listen\",\"state\":\"start\"";
    if (mode == 0) startString += ",\"mode\":\"auto\"}";
    else if (mode == 1) startString += ",\"mode\":\"manual\"}";
    else if (mode == 2) startString += ",\"mode\":\"realtime\"}";

    try {
        websocket_send_text(startString.data(), startString.size());
        // std::cout << "Send: " << startString << std::endl;
    } catch (const std::exception& e) {
        std::cout << "Error sending message: " << e.what() << std::endl;
    }
}
