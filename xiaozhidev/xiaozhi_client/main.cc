// SPDX-License-Identifier: GPL-3.0-only
/*
 * Copyright (c) 2025, Canaan Bright Sight Co., Ltd
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */
#include "control_center.h"
#include "acap.h"
#include <stdio.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <cctype>
#include <string>
#include <string.h>
#include <iostream>
#include <algorithm>
#include <pthread.h>
#include <stdbool.h>
#include <stdlib.h>
#include <thread>
#include <mutex>
#include <atomic>
#include <chrono>
#include <cstdio>
#include "json.hpp"
#include <zmq.hpp>

#include "drive/shared_state.hpp"

#include "acap.h"
#include "cfg.h"
#include "ipc_udp.h"
#include "wake_word_generator.h"
#include "webttsctrl.h"

extern WebttsConfig g_webtts_config;

using json = nlohmann::json;

extern int g_websocket_start;
static WakeupMethodMode g_wakeup_method = kWakeupMethodModeUnknown; // 默认值
extern int g_audio_upload_enable;
SpeechInteractionMode g_speech_interaction_mode = kSpeechInteractionModeAuto;
int g_wakeup_word_start = 0;

static std::atomic<bool> g_serial_running(false);
static std::thread g_serial_worker;
static std::mutex g_serial_mutex;

enum class TouchGestureType {
    Unknown = 0,
    Tap,
    DoubleTap,
    LongPress
};

struct TouchGestureInfo {
    TouchGestureType type{TouchGestureType::Unknown};
    bool left_side{false};
    bool right_side{false};
    uint64_t timestamp_ms{0};
};

static constexpr char kXiaozhiStateSocket[] = "/tmp/doly_xiaozhi_state.sock";
static constexpr char kStateTopic[] = "status.xiaozhi.state";
static constexpr char kEmotionTopic[] = "status.xiaozhi.emotion";
static uint64_t g_json_touch_sequence = 0;

static void stop_current_interaction();
static void process_touch_gesture(const TouchGestureInfo& info);
static TouchGestureInfo parse_touch_gesture_json(const json& payload);
static TouchGestureInfo parse_touch_gesture_shared(const doly::drive::SharedState& shared);
static void publish_status(zmq::socket_t& pub, const std::string& topic, const json& payload);

p_ipc_endpoint_t g_ipc_wakeup_detect_audio_ep;
p_ipc_endpoint_t g_ipc_wakeup_detect_control_ep;

static int _do_wakeup_word_detected();

#define VIDEO_WAKEUP_KEYWORD "你好Doly，我是"
static std::string g_video_wakeup_person_name;

static void audio_data_upload_cb(const unsigned char* data, int length)
{
    send_audio(data,length,NULL);
}

static void wakeup_data_upload_cb(const unsigned char* data, int length)
{
    send_audio(data,length,NULL);
}

static void generate_speech_cb(const char* pcm_data, size_t data_size)
{
    send_audio((const unsigned char*)pcm_data,data_size,NULL);
}

static void audio_data_download_cb(const unsigned char* data, int length)
{
    // printf("[main] audio_data_download_cb called: length=%d, play_opus_stream=%p\n", length, play_opus_stream);
    play_opus_stream(data, length);
    // printf("[main] audio_data_download_cb finished\n");
}

static void audio_data_tts_state_cb(int tts_state)
{
    //printf("========audio tts state===========:%d\n",tts_state);
    //set_tts_state(tts_state);
}

static  void ws_work_state_cb(bool work_state)
{
    if(!work_state)
    {
        json j;
        j["type"] = "wake-up";
        j["status"] = "stop";
        j["wake-up_method"] = g_wakeup_method == kWakeupMethodModeVoice ? "voice" : "video";
        std::string wakeupString = j.dump();

        g_ipc_wakeup_detect_control_ep->send(g_ipc_wakeup_detect_control_ep, wakeupString.data(), wakeupString.size());

    }

}

//解析命令参数
static int parse_command_args(int argc, char **argv)
{
    if (argc > 1) {
        std::string mode_arg = argv[1];
        if (mode_arg == "auto") {
            g_speech_interaction_mode = kSpeechInteractionModeAuto;
        } else if (mode_arg == "manual") {
            g_speech_interaction_mode = kSpeechInteractionModeManual;
        } else if (mode_arg == "realtime") {
            g_speech_interaction_mode = kSpeechInteractionModeRealtime;
        } else if (mode_arg == "wakeup") {
            g_speech_interaction_mode = kSpeechInteractionModeAutoWithWakeupWord;
        } else {
            std::cerr << "Invalid interaction mode: " << mode_arg << std::endl;
            return -1;
        }
        std::cout << "Speech interaction mode set to: " << mode_arg << std::endl;
    } else {
        std::cerr << "Usage: xiaozhi_client [auto|manual|realtime|wakeup]" << std::endl;
        std::cerr << "  auto: Automatic speech interaction mode" << std::endl;
        std::cerr << "  manual: Manual speech interaction mode" << std::endl;
        std::cerr << "  realtime: Real-time speech interaction mode" << std::endl;
        std::cerr << "  wakeup: Automatic speech interaction mode with wakeup word" << std::endl;
        return -1;
    }

    return 0;
}


static int process_wakeup_word_audio_info(char *buffer, size_t size, void *user_data)
{

    return 0;
}

static int _do_wakeup_word_detected()
{
    //设置唤醒词开始标志
    g_wakeup_word_start = 1;

    //唤醒小智
    if (!g_websocket_start)
    {
        connect_to_xiaozhi_server();
        usleep(1 * 1000 * 500);
    }
    else
    {
        //终止当前对话
        g_audio_upload_enable = 0;
        usleep(1000*500);
        printf("_do_wakeup_word_detected -> 终止当前对话\n");
        abort_cur_session();
        usleep(1000*500);
        g_audio_upload_enable = 1;
    }

    //发送唤醒词
    if (g_wakeup_method == kWakeupMethodModeVoice)
    {
        //发送唤醒词 读取预录制的音频文件，然后用opus编码发送给服务器识别？
        // printf("_do_wakeup_word_detected -> 跳过 发送唤醒词 读取预录制的音频文件，然后用opus编码发送给服务器识别？\n");

        // 调用play命令播放 assets/tts_notify.mp3
        // assets/sounds/game/clear-combo-4-394493.mp3
        system("play -q /.doly/sounds/game/clear-combo-4-394493.mp3");
        sleep(0.1); // 等待播放完成
        // char cwd[1024];
        // if (getcwd(cwd, sizeof(cwd)) != NULL) {
        //     std::string filePath = std::string(cwd) + "/wakeup_audio.pcm";
        //     printf("Wakeup word file path: %s\n", filePath.c_str());
        //     //发送唤醒词
        //     wake_word_file(filePath.c_str());
        // } else {
        //     perror("getcwd() error");
        //     return -1;
        // }
    }
    else if (g_wakeup_method == kWakeupMethodModeVideo)
    {
        //发送唤醒词
        //wake_word_file("/usr/bin/wakeup_video.pcm");
        string wakeup_text = VIDEO_WAKEUP_KEYWORD + g_video_wakeup_person_name;
        webtts_send_text(wakeup_text.c_str());
    }
    else
    {
        printf("wakeup method is unknown\n");
        return -1;
    }

    usleep(1000 * 500);

    g_wakeup_word_start = 0;

    return 0;
}

static void stop_current_interaction()
{
    if (g_device_state == kDeviceStateSpeaking || g_device_state == kDeviceStateListening) {
        send_abort_req();
        set_device_state(kDeviceStateIdle);
        send_device_state();
    }
    system("play -q /.doly/sounds/game/button-394464.mp3");
}

static TouchGestureType gesture_label_to_type(std::string label)
{
    std::transform(label.begin(), label.end(), label.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (label.find("double") != std::string::npos || label.find("2") != std::string::npos) {
        return TouchGestureType::DoubleTap;
    }
    if (label.find("long") != std::string::npos || label.find("press") != std::string::npos) {
        return TouchGestureType::LongPress;
    }
    if (label.find("tap") != std::string::npos || label.find("touch") != std::string::npos) {
        return TouchGestureType::Tap;
    }
    return TouchGestureType::Unknown;
}

static void assign_side_from_string(const std::string& text, bool& left, bool& right)
{
    std::string lower = text;
    std::transform(lower.begin(), lower.end(), lower.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (lower.find("left") != std::string::npos) {
        left = true;
    }
    if (lower.find("right") != std::string::npos) {
        right = true;
    }
    if (lower.find("both") != std::string::npos) {
        left = true;
        right = true;
    }
}

static TouchGestureInfo parse_touch_gesture_json(const json& payload)
{
    TouchGestureInfo info;
    std::string type_value = payload.value("gesture", "");
    if (type_value.empty()) {
        type_value = payload.value("type", "");
    }
    if (type_value.empty()) {
        type_value = payload.value("event", "");
    }
    if (!type_value.empty()) {
        info.type = gesture_label_to_type(type_value);
    }

    if (payload.contains("count") && payload["count"].is_number()) {
        int count = payload["count"].get<int>();
        if (count >= 2) {
            info.type = TouchGestureType::DoubleTap;
        }
    }

    if (payload.contains("sides") && payload["sides"].is_array()) {
        for (const auto& entry : payload["sides"]) {
            if (entry.is_string()) {
                assign_side_from_string(entry.get<std::string>(), info.left_side, info.right_side);
            }
        }
    }

    if (payload.contains("side") && payload["side"].is_string()) {
        assign_side_from_string(payload["side"].get<std::string>(), info.left_side, info.right_side);
    }

    if (payload.contains("zone") && payload["zone"].is_string()) {
        assign_side_from_string(payload["zone"].get<std::string>(), info.left_side, info.right_side);
    }

    if (!info.left_side || !info.right_side) {
        if (payload.contains("location") && payload["location"].is_string()) {
            assign_side_from_string(payload["location"].get<std::string>(), info.left_side, info.right_side);
        }
    }

    if (info.timestamp_ms == 0) {
        info.timestamp_ms = payload.value("gesture_time_ms", 0ull);
        if (info.timestamp_ms == 0) {
            info.timestamp_ms = payload.value("timestamp_ms", 0ull);
        }
        if (info.timestamp_ms == 0) {
            info.timestamp_ms = payload.value("time", 0ull);
        }
        if (info.timestamp_ms == 0) {
            info.timestamp_ms = payload.value("ts", 0ull);
        }
        if (info.timestamp_ms == 0) {
            info.timestamp_ms = ++g_json_touch_sequence;
        }
    }

    if (info.type == TouchGestureType::Unknown && payload.contains("gesture_type") && payload["gesture_type"].is_number()) {
        int type_num = payload["gesture_type"].get<int>();
        if (type_num == 2) {
            info.type = TouchGestureType::DoubleTap;
        } else if (type_num == 3) {
            info.type = TouchGestureType::LongPress;
        } else if (type_num == 1) {
            info.type = TouchGestureType::Tap;
        }
    }

    return info;
}

static TouchGestureInfo parse_touch_gesture_shared(const doly::drive::SharedState& shared)
{
    TouchGestureInfo info;
    uint64_t gesture_time = shared.touch.gesture_time_ms.load();
    if (gesture_time == 0) {
        return info;
    }
    info.timestamp_ms = gesture_time;
    // printf("[touch] gesture_time_ms: %llu\n", gesture_time);
    switch (shared.touch.gesture_type.load()) {
        case 1:
            info.type = TouchGestureType::Tap;
            break;
        case 2:
            info.type = TouchGestureType::DoubleTap;
            break;
        case 3:
            info.type = TouchGestureType::LongPress;
            break;
        default:
            info.type = TouchGestureType::Unknown;
            break;
    }
    uint8_t zone = shared.touch.zone.load();
    info.left_side = (zone & 0x01) != 0;
    info.right_side = (zone & 0x02) != 0;
    if (!info.left_side && !info.right_side) {
        if (shared.touch.touched.load()) {
            info.left_side = true;
        }
    }
    return info;
}

static void process_touch_gesture(const TouchGestureInfo& info)
{
    if (info.type == TouchGestureType::Tap) {
        // printf("[touch] single tap -> interrupt interaction\n");
        stop_current_interaction();
    } else if (info.type == TouchGestureType::DoubleTap) {
        // printf("[touch] double tap -> wakeup\n");
        g_wakeup_method = kWakeupMethodModeVoice;
        _do_wakeup_word_detected();
    } else if (info.type == TouchGestureType::LongPress && info.left_side && info.right_side) {
        // printf("[touch] long press both sides -> interrupt interaction\n");
        stop_current_interaction();
    }
}

static void publish_status(zmq::socket_t& pub, const std::string& topic, const json& payload)
{
    try {
        pub.send(zmq::buffer(topic), zmq::send_flags::sndmore);
        auto body = payload.dump();
        pub.send(zmq::buffer(body), zmq::send_flags::none);
    } catch (const zmq::error_t& e) {
        printf("[serial_events] failed to publish %s: %s\n", topic.c_str(), e.what());
    }
}

// 移除 process_wakeup_word_control_info 函数，因为唤醒现在通过串口事件处理

static int process_wakeup_word_control_info(char *buffer, size_t size, void *user_data)
{
    // printf("Received wake-up message:%s\n",buffer);
    //Received wake-up message:{"type": "wake-up", "status": "start", "wake-up_method": "voice", "wake-up_text": "hey_mycroft"}
    if (g_speech_interaction_mode != kSpeechInteractionModeAutoWithWakeupWord)
    {
        // printf("current mode is not kSpeechInteractionModeAutoWithWakeupWord\n");
        return 0;
    }

    json j = json::parse(buffer);
    if (!j.contains("type"))
        return 0;

    if (j["type"] == "wake-up")
    {
        if (!j.contains("wake-up_method"))
            return 0;
        if (!j.contains("status"))
            return 0;

        //开始唤醒
        if (j["status"] == "start")
        {
            std::string wakeup_method = j["wake-up_method"];
            if (wakeup_method == "voice")
            {
                // printf("process_wakeup_word_control_info -> 设置唤醒方法：g_wakeup_method = kWakeupMethodModeVoice \n");
                g_wakeup_method = kWakeupMethodModeVoice;
            }
            else if (wakeup_method == "video")
            {
                g_wakeup_method = kWakeupMethodModeVideo;
                if (!j.contains("wake-up_text"))
                {
                    // printf("wakeup method is video, but no wakeup text\n");
                    return 0;
                }

                g_video_wakeup_person_name = j["wake-up_text"];
                //printf("wakeup method is video, wakeup text is %s\n", g_video_wakeup_person_name.c_str());
            }
            else
            {
                printf("wakeup method is unknown\n");
                return 0;
            }

            _do_wakeup_word_detected();
        }

    }

    return 0;
}


static void init_wakeup_detect()
{
    // 移除语音、视频唤醒词检测 IPC，因为现在使用串口唤醒
    g_ipc_wakeup_detect_audio_ep = ipc_endpoint_create_udp(WAKEUP_WORD_DETECTION_AUDIO_PORT_DOWN, WAKEUP_WORD_DETECTION_AUDIO_PORT_UP, process_wakeup_word_audio_info, NULL);
    g_ipc_wakeup_detect_control_ep = ipc_endpoint_create_udp(WAKEUP_WORD_DETECTION_CONTROL_PORT_DOWN, WAKEUP_WORD_DETECTION_CONTROL_PORT_UP, process_wakeup_word_control_info, NULL);

    // 本地文件语音合成检测
    init_wake_word_generator(wakeup_data_upload_cb);

    g_webtts_config.audio_callback = generate_speech_cb;

    int ret2 = webtts_init(&g_webtts_config);
    if (ret2 != 0) {
        std::cerr << "Failed to initialize WebTTS" << std::endl;
        return ;
    }

}

static void init_serial_events()
{
    if (g_serial_running.load()) {
        return;
    }

    g_serial_running.store(true);

    g_serial_worker = std::thread([&]() {
        zmq::context_t context(1);
        zmq::socket_t sub(context, zmq::socket_type::sub);
        zmq::socket_t state_pub(context, zmq::socket_type::pub);

        int rcvtimeo = 500;
        sub.setsockopt(ZMQ_RCVTIMEO, &rcvtimeo, sizeof(rcvtimeo));
        const char* topic = "event.audio.";
        sub.setsockopt(ZMQ_SUBSCRIBE, topic, std::strlen(topic));
        const char* touch_topic = "io.pca9535.touch.gesture";
        sub.setsockopt(ZMQ_SUBSCRIBE, touch_topic, std::strlen(touch_topic));

        try {
            sub.connect("ipc:///tmp/doly_serial_pub.sock");
        } catch (const zmq::error_t& e) {
            std::cerr << "[serial_events] failed to connect to ipc:///tmp/doly_zmq.sock: " << e.what() << std::endl;
            g_serial_running.store(false);
            return;
        }

        bool state_pub_ready = true;
        std::remove(kXiaozhiStateSocket);
        std::string state_endpoint = std::string("ipc://") + kXiaozhiStateSocket;
        try {
            state_pub.bind(state_endpoint);
        } catch (const zmq::error_t& e) {
            state_pub_ready = false;
            std::cerr << "[serial_events] failed to bind state socket: " << e.what() << std::endl;
        }

        DeviceState last_published_state = g_device_state;
        std::string last_published_emotion = get_current_emotion();
        auto publish_now = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        if (state_pub_ready) {
            publish_status(state_pub, kStateTopic,
                           json{{"state", static_cast<int>(last_published_state)}, {"timestamp_ms", publish_now}});
            publish_status(state_pub, kEmotionTopic,
                           json{{"emotion", last_published_emotion}, {"timestamp_ms", publish_now}});
        }

        int shm_fd = -1;
        doly::drive::SharedState* shared_state = nullptr;
        shm_fd = shm_open(doly::drive::SHARED_STATE_NAME, O_RDONLY, 0666);
        if (shm_fd >= 0) {
            shared_state = static_cast<doly::drive::SharedState*>(
                mmap(nullptr, doly::drive::SHARED_STATE_SIZE, PROT_READ, MAP_SHARED, shm_fd, 0));
            if (shared_state == MAP_FAILED) {
                std::cerr << "[serial_events] failed to map shared state" << std::endl;
                shared_state = nullptr;
                close(shm_fd);
                shm_fd = -1;
            }
        } else {
            std::cerr << "[serial_events] cannot open shared state" << std::endl;
        }

        uint64_t last_json_touch_ts = 0;
        uint64_t last_shared_touch_ts = 0;

        while (g_serial_running.load()) {
            zmq::message_t topic_msg;
            zmq::recv_result_t result = sub.recv(topic_msg, zmq::recv_flags::none);

            if (result) {
                zmq::message_t payload_msg;
                if (sub.recv(payload_msg, zmq::recv_flags::none)) {
                    std::string topic_str(static_cast<char*>(topic_msg.data()), topic_msg.size());
                    std::string payload_str(static_cast<char*>(payload_msg.data()), payload_msg.size());
                    std::lock_guard<std::mutex> lock(g_serial_mutex);
                    if (topic_str == "event.audio.cmd_iHelloDoly") {
                        // printf("[serial_events] Wakeup word detected event received\n");
                        if (g_speech_interaction_mode != kSpeechInteractionModeAutoWithWakeupWord) {
                            // printf("不是唤醒模式！\n");
                        } else {
                            g_wakeup_method = kWakeupMethodModeVoice;
                            if (g_device_state == kDeviceStateIdle) {
                                // printf("等待唤醒状态，切换到语音聊天 _do_wakeup_word_detected ！\n");
                                _do_wakeup_word_detected();
                            } else if (g_device_state == kDeviceStateSpeaking || g_device_state == kDeviceStateListening) {
                                // printf("语音聊天状态，打断并切换到聆听 send_start_listening_req ！\n");
                                _do_wakeup_word_detected();
                            }
                        }
                        // printf("未匹配到 ！\n");
                    } else if (topic_str == "event.audio.cmd_iShutup") {
                        stop_current_interaction();
                    } else if (topic_str.find("event.audio.cmd_iInterrupt") == 0) {
                        stop_current_interaction();
                    // } else if (topic_str == "io.pca9535.touch.gesture") {
                    //     std::cout << "[serial_events] touch gesture event received" << std::endl;
                    //     try {
                    //         auto touch_json = json::parse(payload_str);
                    //         printf("[serial_events] touch gesture payload: %s\n", payload_str.c_str());
                    //         auto info = parse_touch_gesture_json(touch_json);
                    //         printf("[serial_events] parsed touch gesture: type=%d, left=%d, right=%d, timestamp_ms=%llu\n",
                    //                static_cast<int>(info.type), info.left_side, info.right_side, info.timestamp_ms);
                    //         if (info.timestamp_ms > last_json_touch_ts) {
                    //             last_json_touch_ts = info.timestamp_ms;
                    //             process_touch_gesture(info);
                    //         }
                    //     } catch (const json::exception& e) {
                    //         std::cerr << "[serial_events] invalid touch payload: " << e.what() << std::endl;
                    //     }
                    }
                }
            }

            if (shared_state) {
                auto info = parse_touch_gesture_shared(*shared_state);
                if (info.type != TouchGestureType::Unknown && info.timestamp_ms > last_shared_touch_ts) {
                    last_shared_touch_ts = info.timestamp_ms;
                    std::lock_guard<std::mutex> lock(g_serial_mutex);
                    process_touch_gesture(info);
                }
            }

            if (state_pub_ready) {
                auto now = std::chrono::system_clock::now();
                auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                    now.time_since_epoch()).count();
                DeviceState current_state = g_device_state;
                if (current_state != last_published_state) {
                    last_published_state = current_state;
                    publish_status(state_pub, kStateTopic,
                                   json{{"state", static_cast<int>(current_state)}, {"timestamp_ms", now_ms}});
                }
                std::string current_emotion = get_current_emotion();
                if (current_emotion != last_published_emotion) {
                    last_published_emotion = current_emotion;
                    publish_status(state_pub, kEmotionTopic,
                                   json{{"emotion", current_emotion}, {"timestamp_ms", now_ms}});
                }
            }
        }

        if (shared_state) {
            munmap(shared_state, doly::drive::SHARED_STATE_SIZE);
        }
        if (shm_fd >= 0) {
            close(shm_fd);
        }
        sub.close();
        state_pub.close();
    });
}

static int _test_wakeup()
{
    while(1)
    {
        printf("Press Enter to continue...\n");
        while (getchar() != '\n'); // 等待用户按下回车键

        static int count = 0;
        count ++;
        printf("[%d]========test wakeup word=========\n",count);
        if (count % 2 == 0)
            g_wakeup_method = kWakeupMethodModeVideo;
        else
            g_wakeup_method = kWakeupMethodModeVoice;

        _do_wakeup_word_detected();
    }
    return 0;
}




int main(int argc, char **argv)
{
    int ret = 0;
    //解析命令参数
    ret = parse_command_args(argc, argv);
    if (ret != 0)
    {
        return -1;
    }

    //初始化设备
    ret = init_device(audio_data_download_cb,audio_data_tts_state_cb,ws_work_state_cb);
    if (ret != 0)
    {
        printf("init_device failed\n");
        return -1;
    }

    //激活设备
    ret = active_device();
    if (ret != 0)
    {
        printf("active_device failed\n");
        return -1;
    }

    //初始化唤醒词检测
    init_wakeup_detect();

    //初始化 serial 事件订阅
    init_serial_events();

    //监听音频前端事件
    // init_audio_frontend_events();

    //初始化音频
    acap_init(audio_data_upload_cb);

    //启动音频
    acap_start();

    //连接到小智服务器
    if (g_speech_interaction_mode != kSpeechInteractionModeAutoWithWakeupWord) {
        connect_to_xiaozhi_server();
    }
    else
    {
        //测试唤醒词
        // printf("=========test wakeup\n");
        // _test_wakeup();

    }

    while (1)
    {
        sleep(1);
    }

    // 停止 serial 事件监听
    g_serial_running.store(false);
    if (g_serial_worker.joinable()) {
        g_serial_worker.join();
    }

    // xiaozhi::audio::stop_audio_frontend_events();
    acap_stop();
    acap_deinit();

    return 0;
}