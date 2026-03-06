#include <websocketpp/config/asio_client.hpp>
#include <websocketpp/client.hpp>

#include <iostream>
#include <fstream>
#include <vector>
#include <algorithm>
#include <dirent.h>
#include <random>
#include <sstream>
#include <iomanip>
#include <mutex>
#include "webttsctrl.h"

// Include nlohmann/json library
#include "json.hpp"
#include "websocket_client.h"
#include "http.h"
#include "ipc_udp.h"
#include "uuid.h"

#include "cfg.h"
#include "control_center.h"
#include "control_center_utils.h"
#include "acap.h"
#include "audio_cfg.h"
#include "config_utils.h"
#include "xiaozhi_zmq_publisher.h"

extern SpeechInteractionMode g_speech_interaction_mode;
using json = nlohmann::json;
int g_audio_upload_enable = 1;
static std::string g_session_id;
int g_websocket_start = 0;

WebttsConfig g_webtts_config;
std::string g_ota_url;
std::string g_ws_addr;

static p_ipc_endpoint_t g_ipc_ep_ui;
DeviceState g_device_state = kDeviceStateUnknown;
static std::string g_uuid;
static std::string g_mac;
static websocket_data_t g_ws_data;
static server_audio_data_callback g_callback = NULL;
static audio_tts_state_callback g_tts_callback = NULL;
static ws_work_state_callback g_ws_state_callback = NULL;

static std::string g_current_emotion = "neutral";
static std::mutex g_emotion_mutex;

// 设置设备状态
void set_device_state(DeviceState state)
{
    g_device_state = state;
}

// 发送设备状态
void send_device_state(void)
{
    std::string stateString = "{\"state\":" + std::to_string(g_device_state) + "}";
    if (g_ipc_ep_ui) g_ipc_ep_ui->send(g_ipc_ep_ui, stateString.data(), stateString.size());
}

void set_current_emotion(const std::string& emotion)
{
    std::lock_guard<std::mutex> lock(g_emotion_mutex);
    g_current_emotion = emotion;
}

std::string get_current_emotion()
{
    std::lock_guard<std::mutex> lock(g_emotion_mutex);
    return g_current_emotion;
}

static void send_stt(const std::string& text)
{
    if (!g_ipc_ep_ui) {
        std::cerr << "Error: g_ipc_ep_ui is nullptr" << std::endl;
        return;
    }

    try {
        json j;
        j["text"] = text;
        std::string textString = j.dump();
        g_ipc_ep_ui->send(g_ipc_ep_ui, textString.data(), textString.size());
    } catch (const std::exception& e) {
        std::cerr << "Error creating JSON string: " << e.what() << std::endl;
    }
}


static void process_opus_data_downloaded(const char *buffer, size_t size)
{
    // static int recv_count = 0;
    // if ((++recv_count % 10) == 0 || size > 0) {
    //     printf("[control_center] process_opus_data_downloaded: size=%zu, count=%d, callback=%p\n", 
    //            size, recv_count, g_callback);
    // }
    if (g_callback != NULL) {
        // printf("[control_center] Calling g_callback with %zu bytes\n", size);
        g_callback((const unsigned char *)buffer, size);
        // printf("[control_center] g_callback returned\n");
    } else {
        // printf("[control_center] WARNING: g_callback is NULL, cannot route opus data!\n");
    }
}

// 发送中止请求
void send_abort_req()
{
    websocket_send_abort(g_session_id);
}

// 发送开始监听请求
void send_start_listening_req(ListeningMode mode)
{
    int m = 0;
    if (mode == kListeningModeAutoStop) m = 0;
    else if (mode == kListeningModeManualStop) m = 1;
    else if (mode == kListeningModeAlwaysOn) m = 2;
    websocket_send_listen_start(g_session_id, m);
}

static void process_hello_json(const char *buffer, size_t size)
{
    json j = json::parse(buffer);
    int sample_rate = j["audio_params"]["sample_rate"];
    int channels = j["audio_params"]["channels"];
    std::cout << "Received valid 'hello' message with sample_rate: " << sample_rate << " and channels: " << channels << std::endl;

    int configured_rate = xiaozhi::config::load_playback_sample_rate();
    if (sample_rate != configured_rate) {
        std::cerr << "[control_center] Warning: server playback sample rate " << sample_rate
                  << " Hz differs from configured playback_sample_rate=" << configured_rate
                  << " Hz in " << CFG_FILE << std::endl;
    }

    // 根据服务器指定的采样率重新配置解码器
    if (sample_rate != SAMPLE_RATE) {
        if (acap_set_decoder_sample_rate(sample_rate) == 0) {
            std::cout << "[control_center] Decoder sample rate updated to " << sample_rate << " Hz" << std::endl;
        } else {
            std::cerr << "[control_center] Failed to update decoder sample rate" << std::endl;
        }
    }

    g_session_id = j["session_id"];
#if 0
    std::string desc = R"(
    {"session_id":"","type":"iot","update":true,"descriptors":[{"name":"Speaker","description":"扬声器","properties":{"volume":{"description":"当前音量值","type":"number"}},"methods":{"SetVolume":{"description":"设置音量","parameters":{"volume":{"description":"0到100之间的整数","type":"number"}}}}}]}
    )";

    // Send the new message
    try {
        //c->send(hdl, desc, websocketpp::frame::opcode::text);
        websocket_send_text(desc.data(), desc.size());
        std::cout << "Send: " << desc << std::endl;
    } catch (const websocketpp::lib::error_code& e) {
        std::cout << "Error sending message: " << e << " (" << e.message() << ")" << std::endl;
    }

    std::string desc2 = R"(
    {"session_id":"","type":"iot","update":true,"descriptors":[{"name":"Backlight","description":"屏幕背光","properties":{"brightness":{"description":"当前亮度百分比","type":"number"}},"methods":{"SetBrightness":{"description":"设置亮度","parameters":{"brightness":{"description":"0到100之间的整数","type":"number"}}}}}]}
)";

    // Send the new message

    try {
        //c->send(hdl, desc2, websocketpp::frame::opcode::text);
        websocket_send_text(desc2.data(), desc2.size());
        std::cout << "Send: " << desc2 << std::endl;
    } catch (const websocketpp::lib::error_code& e) {
        std::cout << "Error sending message: " << e << " (" << e.message() << ")" << std::endl;
    }

    std::string desc3 = R"(
    {"session_id":"","type":"iot","update":true,"descriptors":[{"name":"Battery","description":"电池管理","properties":{"level":{"description":"当前电量百分比","type":"number"},"charging":{"description":"是否充电中","type":"boolean"}},"methods":{}}]}
)";

    // Send the new message

    try {
        //c->send(hdl, desc3, websocketpp::frame::opcode::text);
        websocket_send_text(desc3.data(), desc3.size());
        std::cout << "Send: " << desc3 << std::endl;
    } catch (const websocketpp::lib::error_code& e) {
        std::cout << "Error sending message: " << e << " (" << e.message() << ")" << std::endl;
    }
#endif

    std::string startString;
    // printf("Sending start listening request\n");
    if (g_speech_interaction_mode == kSpeechInteractionModeAuto) {
        startString = R"({"session_id":"","type":"listen","state":"start","mode":"auto"})";
        // printf("1. kSpeechInteractionModeAuto\n");
    } else if (g_speech_interaction_mode == kSpeechInteractionModeManual) {
        startString = R"({"session_id":"","type":"listen","state":"start","mode":"manual"})";
        // printf("2. kSpeechInteractionModeManual\n");
    } else if (g_speech_interaction_mode == kSpeechInteractionModeRealtime) {
        startString = R"({"session_id":"","type":"listen","state":"start","mode":"realtime"})";
        // printf("3. kSpeechInteractionModeRealtime\n");
    } else if (g_speech_interaction_mode == kSpeechInteractionModeAutoWithWakeupWord) {
        startString = R"({"session_id":"","type":"listen","state":"start","mode":"auto"})";
        // printf("4. kSpeechInteractionModeAutoWithWakeupWord\n");
    }

    try {
        //c->send(hdl, startString, websocketpp::frame::opcode::text);
        // printf("Sending start listening request: %s\n", startString.c_str());
        websocket_send_text(startString.data(), startString.size());
        std::cout << "Send: " << startString << std::endl;
    } catch (const websocketpp::lib::error_code& e) {
        std::cout << "Error sending message: " << e << " (" << e.message() << ")" << std::endl;
    }

    g_audio_upload_enable = 1;

#if 0
    std::string state = R"(
        {"session_id":"","type":"iot","update":true,"states":[{"name":"Speaker","state":{"volume":80}},{"name":"Backlight","state":{"brightness":75}},{"name":"Battery","state":{"level":0,"charging":false}}]}
    )";

    try {
        //c->send(hdl, state, websocketpp::frame::opcode::text);
        websocket_send_text(state.data(), state.size());
        std::cout << "Send: " << state << std::endl;
    } catch (const websocketpp::lib::error_code& e) {
        std::cout << "Error sending message: " << e << " (" << e.message() << ")" << std::endl;
    }
#endif

}

// static void interrupt(){
//     send_start_listening_req(kListeningModeAutoStop);
//     set_device_state(kDeviceStateListening);
//     send_device_state();

//     g_audio_upload_enable = 1;
// }

static void process_other_json(const char *buffer, size_t size)
{
    try {
        // Parse JSON data
        json j = json::parse(buffer);

        if (!j.contains("type"))
            return;

        if (j["type"] == "tts") {
            auto state = j["state"];
            if (state == "start") {
                // 下发语音, 可以关闭录音
                g_audio_upload_enable = 0;
                set_device_state(kDeviceStateListening);
                send_device_state();
                if (g_tts_callback != NULL) {
                    g_tts_callback(1);
                }
            } else if (state == "stop") {
                if (g_speech_interaction_mode == kSpeechInteractionModeAuto || g_speech_interaction_mode == kSpeechInteractionModeAutoWithWakeupWord) {
                    // 本次交互结束, 可以继续上传声音
                    // 等待一会以免她听到自己的话误以为再次对话
                    sleep(2);
                    send_start_listening_req(kListeningModeAutoStop);
                } else if (g_speech_interaction_mode == kSpeechInteractionModeManual) {
                    // 本次交互结束, 可以继续上传声音
                    // 等待一会以免她听到自己的话误以为再次对话
                    sleep(2);
                    send_start_listening_req(kListeningModeManualStop);
                } else if (g_speech_interaction_mode == kSpeechInteractionModeRealtime) {
                    ;// do nothing
                }

                set_device_state(kDeviceStateListening);
                send_device_state();

                if (g_tts_callback != NULL)
                    g_tts_callback(0);

                g_audio_upload_enable = 1;
            } else if (state == "sentence_start") {
                // 取出"text", 通知GUI
                // {"type":"tts","state":"sentence_start","text":"1加1等于2啦~","session_id":"eae53ada"}
                auto text = j["text"];
                send_stt(text.get<std::string>());
                // 服务器的回复文本
                // printf("-----------------> [control_center] 服务器的回复文本: %s\n", text.get<std::string>().c_str());
                //send_start_listening_req(kListeningModeAlwaysOn);
                set_device_state(kDeviceStateSpeaking);
                send_device_state();
            }
        } else if (j["type"] == "stt") {
            // 表示服务器端识别到了用户语音, 取出"text", 通知GUI
            auto text = j["text"];
            send_stt(text.get<std::string>());
            // 识别到的语音文本
            // printf("-----------------> [control_center] 识别到的语音文本: %s\n", text.get<std::string>().c_str());
            // 模拟本机 NLU处理，不经过小智
        } else if (j["type"] == "llm") {
            // 有"happy"等取值
        /*
            "neutral",
            "happy",
            "laughing",
            "funny",
            "sad",
            "angry",
            "crying",
            "loving",
            "embarrassed",
            "surprised",
            "shocked",
            "thinking",
            "winking",
            "cool",
            "relaxed",
            "delicious",
            "kissy",
            "confident",
            "sleepy",
            "silly",
            "confused"
        */
        auto emotion = j["emotion"];
        std::string emotion_str = emotion.get<std::string>();
        // printf("-----------------> [control_center] 识别到情感: %s\n", emotion_str.c_str());
        set_current_emotion(emotion_str);
        
        // 处理结构化响应并发布到 ZMQ
        xiaozhi::XiaozhiZmqPublisher::instance().process_llm_response(j);
            
        } else if (j["type"] == "iot") {

        }
    } catch (json::parse_error& e) {
        std::cout << "Failed to parse JSON message: " << e.what() << std::endl;
    } catch (std::exception& e) {
        std::cout << "Error processing message: " << e.what() << std::endl;
    }
}

static void process_txt_data_downloaded(const char *buffer, size_t size)
{
    try {
        // Parse the JSON message
        json j = json::parse(buffer);

        // Check if the message matches the expected structure
        if (j.contains("type") && j["type"] == "hello") {
            process_hello_json(buffer, size);
        } else {
            process_other_json(buffer, size);
        }

    } catch (json::parse_error& e) {
        std::cout << "Failed to parse JSON message: " << e.what() << std::endl;
    } catch (std::exception& e) {
        std::cout << "Error processing message: " << e.what() << std::endl;
    }
}

static void ws_work_state_cb(bool work_state)
{
    if (g_ws_state_callback != NULL) {
        g_ws_state_callback(work_state);
    }
}

int  send_audio(const unsigned char *buffer, int size, void *user_data)
{
    if (!g_websocket_start)
    {
        return 0;
    }

    if (g_speech_interaction_mode == kSpeechInteractionModeAuto || g_speech_interaction_mode == kSpeechInteractionModeManual || g_speech_interaction_mode == kSpeechInteractionModeAutoWithWakeupWord) {
        if (g_audio_upload_enable) {
            websocket_send_binary((const char *)buffer, size);
            // static int cnt = 0;
            // if ((cnt++ % 100) == 0)
            //     std::cout << "Send opus data to server: " << size <<" count: "<< cnt << std::endl;
        }
    }
    else if (g_speech_interaction_mode == kSpeechInteractionModeRealtime) {
        // 直接发送数据到服务器
        websocket_send_binary((const char *)buffer, size);
        // static int cnt = 0;
        // if ((cnt++ % 100) == 0)
        //     std::cout << "Send opus data to server: " << size <<" count: "<< cnt << std::endl;
    }

    return 0;
}

int process_ui_data(char *buffer, size_t size, void *user_data)
{
    return 0;
}

/**
 * 从配置文件中读取 UUID
 *
 * 该函数尝试从 /etc/xiaozhi.cfg 文件中读取 UUID。
 * 如果文件存在且包含有效的 UUID，则返回该 UUID。
 * 否则，返回空字符串。
 *
 * @return 从配置文件中读取的 UUID，如果未找到则返回空字符串
 */
// utility functions are implemented in control_center_utils.cc

int  active_device()
{
    char active_code[20] = "";
    static http_data_t http_data;
    // http_data.url = "https://api.tenclass.net/xiaozhi/ota/";
    http_data.url = g_ota_url;

    // 替换 http_data.post 中的 uuid
    std::ostringstream post_stream;
    post_stream << R"(
        {
            "uuid":")" << g_uuid << R"(",
            "application": {
                "name": "xiaozhi_linux_k230",
                "version": "1.0.0"
            },
            "ota": {
            },
            "board": {
                "type": "k230_linux_board",
                "name": "k230_linux_board"
            }
        }
    )";
    http_data.post = post_stream.str();

    // 替换 http_data.headers 中的 Device-Id
    std::ostringstream headers_stream;
    headers_stream << R"(
        {
            "Content-Type": "application/json",
            "Device-Id": ")" << g_mac << R"(",
            "User-Agent": "canaan",
            "Accept-Language": "zh-CN"
        }
    )";
    http_data.headers = headers_stream.str();

    while (0 != active_device(&http_data, active_code)) {
        std::cout << "active_device failed" << std::endl;
        if (active_code[0]) {
            std::string auth_code = "Active-Code: " + std::string(active_code);
            set_device_state(kDeviceStateActivating);
            send_device_state();
            send_stt(auth_code);
        }
        sleep(5);
    }

    set_device_state(kDeviceStateIdle);
    send_device_state();
    send_stt("设备已经激活");

    return 0;
}

// parseWebSocketUrl is implemented in control_center_utils.cc

int  init_device(server_audio_data_callback callback,audio_tts_state_callback tts_callback,ws_work_state_callback ws_state_callback)
{
    g_callback = callback;
    g_tts_callback = tts_callback;
    g_ws_state_callback = ws_state_callback;

    // 获取无线网卡的 MAC 地址
    std::string mac = get_wireless_mac_address();
    if (mac.empty()) {
        std::cerr << "Failed to get wireless MAC address" << std::endl;
        mac = "00:00:00:00:00:00"; // 默认值，可以根据需要修改
    }

    // 读取配置文件中的 UUID
    std::string uuid = read_uuid_from_config();
    if (uuid.empty()) {
        std::cerr << "UUID not found in " CFG_FILE << std::endl;
        // 生成新的 UUID
        uuid = generate_uuid();
        std::cout << "Generated new UUID: " << uuid << std::endl;

        // 将新的 UUID 写入配置文件
        if (!write_uuid_to_config(uuid)) {
            std::cerr << "Failed to write UUID to " CFG_FILE << std::endl;
        } else {
            std::cout << "UUID written to " CFG_FILE << std::endl;
        }
    } else {
        std::cout << "UUID from " CFG_FILE ": " << uuid << std::endl;
    }

    // 读取配置文件中的 WebTTS 配置
    if (!is_webtts_config_exists()) {
        std::cerr << "WebTTS config not found in " CFG_FILE << std::endl;
        // 创建默认配置
        WebttsConfig default_config = create_default_webtts_config();
        // 将默认配置写入配置文件
        if (!write_webtts_to_config(default_config)) {
            std::cerr << "Failed to write WebTTS config to " CFG_FILE << std::endl;
        } else {
            std::cout << "WebTTS config written to " CFG_FILE << std::endl;
        }
    }

    if (!read_webtts_from_config(g_webtts_config)) {
        std::cerr << "Failed to read WebTTS config from " CFG_FILE << std::endl;
        return -1;
    }

    // 读取配置文件中的 xiaozhi配置
    if (!is_xiaozhi_config_exists()) {
        std::cerr << "xiaozhi config not found in " CFG_FILE << std::endl;
        // 创建默认配置
        create_default_xiaozhi_config(g_ota_url, g_ws_addr);
        // 将默认配置写入配置文件
    if (!write_xiaozhi_to_config(g_ota_url, g_ws_addr, xiaozhi::config::kDefaultPlaybackSampleRate)) {
            std::cerr << "Failed to write xiaozhi config to " CFG_FILE << std::endl;
        } else {
            std::cout << "xiaozhi config written to " CFG_FILE << std::endl;
        }
    }

    if (!read_xiaozhi_from_config(g_ota_url, g_ws_addr)) {
        std::cerr << "Failed to read xiaozhi config from " CFG_FILE << std::endl;
        return -1;
    }

    g_uuid = uuid;
    g_mac = mac;

    g_ipc_ep_ui = ipc_endpoint_create_udp(UI_PORT_UP, UI_PORT_DOWN, process_ui_data, NULL);

    // 替换 g_ws_data.headers 中的 Device-Id 和 Client-Id
    std::ostringstream ws_headers_stream;
    ws_headers_stream << R"(
        {
            "Authorization": "Bearer test-token",
            "Protocol-Version": "1",
            "Device-Id": ")" << g_mac << R"(",
            "Client-Id": ")" << g_uuid << R"("
        }
    )";
    g_ws_data.headers = ws_headers_stream.str();

    g_ws_data.hello = R"(
        {
            "type": "hello",
            "version": 1,
            "transport": "websocket",
            "audio_params": {
                "format": "opus",
                "sample_rate": 16000,
                "channels": 1,
                "frame_duration": 60
            }
        })";


    // g_ws_data.hostname = "api.tenclass.net";
    // g_ws_data.port = "443";
    // g_ws_data.path = "/xiaozhi/v1/";

    parseWebSocketUrl(g_ws_addr, g_ws_data);
    printf("ws_addr:%s\n", g_ws_addr.c_str());
    printf("ws_hostname:%s\n", g_ws_data.hostname.c_str());
    printf("ws_port:%s\n", g_ws_data.port.c_str());
    printf("ws_path:%s\n", g_ws_data.path.c_str());



    websocket_set_callbacks(process_opus_data_downloaded, process_txt_data_downloaded, &g_ws_data,ws_work_state_cb);
    
    // 初始化 ZMQ 发布器
    if (!xiaozhi::XiaozhiZmqPublisher::instance().init()) {
        std::cerr << "Warning: Failed to initialize ZMQ publisher" << std::endl;
        // 不致命，继续运行
    }
    
    return 0;
}

int  connect_to_xiaozhi_server()
{
    if (g_websocket_start == 0) {
        websocket_start();
        //g_websocket_start = 1;
    }

    return 0;
}

int  abort_cur_session()
{
    if (!g_websocket_start)
    {
        return -1;
    }

    send_abort_req();
    return 0;
}



