#ifndef __WEBTTSCTRL_H__
#define __WEBTTSCTRL_H__
#include <string>
using namespace std;

typedef void (*pcm_callback)(const char* pcm_data, size_t data_size);

typedef struct tag_WebttsConfig {
    std::string app_id;         // APPID
    std::string api_key;        // APIKEY
    std::string api_secret;     // APISECRET
    int sample_rate;            // 采样率，16000
    std::string voice_name;     // 发音人名称，如 "xiaoyan"
    int speed;              // 语速，范围 0-100
    pcm_callback audio_callback;// 语音合成PCM回调函数
} WebttsConfig;

WebttsConfig create_default_webtts_config();
int webtts_init(const WebttsConfig* config);
int webtts_send_text(const char* text);

#endif