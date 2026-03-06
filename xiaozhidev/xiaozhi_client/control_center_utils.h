// Small helpers extracted from control_center for clarity
#pragma once

#include <string>
#include "websocket_client.h"
#include "webttsctrl.h"
#include "json.hpp"

using json = nlohmann::json;

struct websocket_data_t;

std::string read_uuid_from_config();
bool write_uuid_to_config(const std::string& uuid);
bool is_webtts_config_exists();
bool write_webtts_to_config(const WebttsConfig& config);
bool read_webtts_from_config(WebttsConfig& config);
bool is_xiaozhi_config_exists();
bool write_xiaozhi_to_config(const std::string& ota_url, const std::string& ws_addr, int playback_sample_rate = -1);
bool read_xiaozhi_from_config(std::string& ota_url, std::string& ws_addr);

void create_default_xiaozhi_config(std::string &ota_url, std::string &ws_addr);

// websocket url parser
void parseWebSocketUrl(const std::string& url, websocket_data_t& data);

// helpers for sending messages over websocket
void websocket_send_abort(const std::string& session_id);
void websocket_send_listen_start(const std::string& session_id, int mode);
