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
#include "webttsctrl.h"
#include <websocketpp/config/asio_client.hpp>
#include <websocketpp/client.hpp>
#include <websocketpp/common/asio.hpp>
#include <iostream>
#include <string>
#include <openssl/hmac.h>
#include <openssl/evp.h>
#include <iomanip>
#include <sstream>
#include <ctime>
#include "json.hpp"
#include <fstream>
#include <cctype>
#include <algorithm>
#include <iomanip>
#include "opus/opus.h"
#include "audio_cfg.h"

namespace asio = websocketpp::lib::asio;
using namespace std;
using json = nlohmann::json;
static int g_enable_debug = 0;

typedef websocketpp::client<websocketpp::config::asio_tls_client> client;
typedef websocketpp::lib::shared_ptr<websocketpp::lib::asio::ssl::context> context_ptr;

static WebttsConfig g_webtts_config;

// 配置参数 - 替换为你的实际信息
const string HOST = "tts-api.xfyun.cn";
const string PATH = "/v2/tts";
// const string APPID = "5b31efd1";
// const string APIKEY = "9d18e8e9caea0ee6e1984411d82e0a66";
// const string APISECRET = "38599312610808f48a783e8afe1ba6a8";

static char g_pcm_buffer[1024*1024*3]; // 3MB
static int g_pcm_buffer_index = 0;

static OpusEncoder *opus_encoder = NULL;
static char *encoder_input_buffer = NULL;
static uint8_t *encoder_output_buffer = NULL;

// 新增：保存 WebSocket 连接句柄
static websocketpp::connection_hdl g_ws_connection_hdl;
static client* g_ws_client;

// 默认配置初始化函数
WebttsConfig create_default_webtts_config() {
    WebttsConfig config;
    config.app_id = "7cdb8495";
    config.api_key = "b788270f8921a236293aa5f5385aa7b9";
    config.api_secret = "NTAyY2E4MTNlOWNlYmI0MzllZGQ4ZTE1";
    config.sample_rate = 16000;
    config.voice_name = "xiaoyan";
    config.speed = 80;
    config.audio_callback = nullptr; // 默认不设置回调
    return config;
}

static void init_audio_encoder() {
    int encoder_error;
    opus_encoder = opus_encoder_create(SAMPLE_RATE, 1, OPUS_APPLICATION_VOIP,
                                       &encoder_error);
    if (encoder_error != OPUS_OK) {
        printf("Failed to create OPUS encoder");
        return;
    }

    if (opus_encoder_init(opus_encoder, SAMPLE_RATE, 1, OPUS_APPLICATION_VOIP) != OPUS_OK) {
        printf("Failed to initialize OPUS encoder");
        return;
    }
    opus_encoder_ctl(opus_encoder, OPUS_SET_BITRATE(OPUS_ENCODER_BITRATE));
    opus_encoder_ctl(opus_encoder, OPUS_SET_COMPLEXITY(OPUS_ENCODER_COMPLEXITY));
    opus_encoder_ctl(opus_encoder, OPUS_SET_SIGNAL(OPUS_SIGNAL_VOICE));
    encoder_input_buffer = (char *)malloc(BUFFER_SAMPLES * CHANNELS_NUM * BITS_PER_SAMPLE / 8);
    encoder_output_buffer = (uint8_t *)malloc(OPUS_OUT_BUFFER_SIZE);
    printf("%s\n", "init_audio_encoder");
}


// URL 编码函数
static string url_encode(const string &value) {
    ostringstream escaped;
    escaped.fill('0');
    escaped << hex;

    for (string::const_iterator i = value.begin(), n = value.end(); i != n; ++i) {
        string::value_type c = (*i);

        // Keep alphanumeric and other accepted characters intact
        if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            escaped << c;
            continue;
        }

        // Any other characters are percent-encoded
        escaped << uppercase;
        escaped << '%' << setw(2) << int((unsigned char) c);
        escaped << nouppercase;
    }

    return escaped.str();
}

static string base64_encode(const string &input) {
    const char base64_chars[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    string ret;
    int i = 0, j = 0;
    unsigned char char_array_3[3], char_array_4[4];

    for (auto &c : input) {
        char_array_3[i++] = c;
        if (i == 3) {
            char_array_4[0] = (char_array_3[0] & 0xfc) >> 2;
            char_array_4[1] = ((char_array_3[0] & 0x03) << 4) + ((char_array_3[1] & 0xf0) >> 4);
            char_array_4[2] = ((char_array_3[1] & 0x0f) << 2) + ((char_array_3[2] & 0xc0) >> 6);
            char_array_4[3] = char_array_3[2] & 0x3f;

            for(i = 0; i <4; i++)
                ret += base64_chars[char_array_4[i]];
            i = 0;
        }
    }

    if (i) {
        for(j = i; j < 3; j++)
            char_array_3[j] = '\0';

        char_array_4[0] = (char_array_3[0] & 0xfc) >> 2;
        char_array_4[1] = ((char_array_3[0] & 0x03) << 4) + ((char_array_3[1] & 0xf0) >> 4);
        char_array_4[2] = ((char_array_3[1] & 0x0f) << 2) + ((char_array_3[2] & 0xc0) >> 6);

        for (j = 0; j < i + 1; j++)
            ret += base64_chars[char_array_4[j]];

        while(i++ < 3)
            ret += '=';
    }

    return ret;
}

// base64 解码函数
static string base64_decode(const string &input) {
    const std::string base64_chars =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
        "abcdefghijklmnopqrstuvwxyz"
        "0123456789+/";

    std::vector<unsigned char> ret;
    std::vector<int> T(256, -1);
    for (int i = 0; i < 64; i++) T[base64_chars[i]] = i;

    int val = 0, val_bits = -8;
    for (unsigned char c : input) {
        if (T[c] == -1) break;
        val = (val << 6) + T[c];
        val_bits += 6;
        if (val_bits >= 0) {
            ret.push_back(char((val >> val_bits) & 0xFF));
            val_bits -= 8;
        }
    }
    return std::string(ret.begin(), ret.end());
}

// 修改 hmac_sha256 函数
static string hmac_sha256(const string &key, const string &data) {
    unsigned char hash[EVP_MAX_MD_SIZE];
    unsigned int len = 0;

    HMAC_CTX *ctx = HMAC_CTX_new();
    HMAC_Init_ex(ctx, key.c_str(), key.length(), EVP_sha256(), NULL);
    HMAC_Update(ctx, (unsigned char*)data.c_str(), data.length());
    HMAC_Final(ctx, hash, &len);
    HMAC_CTX_free(ctx);

    // 直接返回二进制数据
    return string(reinterpret_cast<char*>(hash), len); // 关键修改点
}

static string get_rfc1123_time() {
    time_t now = time(nullptr);
    struct tm tm = *gmtime(&now);
    char buf[128];
    strftime(buf, sizeof(buf), "%a, %d %b %Y %H:%M:%S GMT", &tm);
    return string(buf);
}

static string get_auth_str() {
    string date = get_rfc1123_time();

    string signature_origin = "host: " + HOST + "\n" +
                             "date: " + date + "\n" +
                             "GET " + PATH + " HTTP/1.1";

    // 打印签名原始字符串
    // cout << "=== Signature Origin Debug ===" << endl;
    // cout << signature_origin << endl;
    // cout << "=============================" << endl;

    string signature_bin = hmac_sha256(g_webtts_config.api_secret , signature_origin);
    string signature = base64_encode(signature_bin); // 直接对二进制数据编码

    // 打印中间签名值
    //cout << "HMAC-SHA256 (raw): " << base64_encode(signature_bin) << endl;

    string authorization_origin =
        "api_key=\"" + g_webtts_config.api_key + "\", "
        "algorithm=\"hmac-sha256\", "
        "headers=\"host date request-line\", "
        "signature=\"" + signature + "\"";

    //cout <<"@@@@@@@@@@@@authorization_origin:\n" <<  authorization_origin << endl;
    return base64_encode(authorization_origin);
}

static context_ptr on_tls_init() {
    context_ptr ctx = websocketpp::lib::make_shared<websocketpp::lib::asio::ssl::context>(asio::ssl::context::tlsv12_client);
    ctx->set_options(SSL_OP_NO_SSLv2 | SSL_OP_NO_SSLv3 | SSL_OP_NO_COMPRESSION);
    ctx->set_verify_mode(asio::ssl::verify_none);
    return ctx;
}

static int _encode_pcm_data(const char* data, int len,bool blast)
{
    memcpy(g_pcm_buffer + g_pcm_buffer_index, data, len);
    g_pcm_buffer_index += len;

    if (blast) {
        //保存文件，用于debug
        std::ofstream out_file("tts.pcm", std::ios::binary);
        out_file.write(g_pcm_buffer, g_pcm_buffer_index);
        out_file.flush();
        out_file.close();

        for (int i = 0; i < g_pcm_buffer_index; i += BUFFER_SAMPLES) {
            int frame_size = opus_encode(opus_encoder, (const opus_int16 *)(g_pcm_buffer + i), BUFFER_SAMPLES, encoder_output_buffer, OPUS_OUT_BUFFER_SIZE);
            if (frame_size < 0) {
                printf("Opus encoding failed: %s\n", opus_strerror(frame_size));
                return -1;
            }
            if (g_webtts_config.audio_callback != NULL)
                g_webtts_config.audio_callback((const char *)encoder_output_buffer, frame_size);
        }

        g_pcm_buffer_index = 0;

        //挂断 WebSocket 连接
        if (g_ws_client &&!g_ws_connection_hdl.expired()) {
            websocketpp::lib::error_code ec;
            g_ws_client->close(g_ws_connection_hdl, websocketpp::close::status::normal, "", ec);
            if (ec) {
                std::cerr << "关闭 WebSocket 连接失败: " << ec.message() << std::endl;
            }
        }
    }

    return 0;
}

int webtts_init(const WebttsConfig* config)
{
    g_webtts_config = *config;
    init_audio_encoder();
    return 0;
}

int webtts_send_text(const char* text)
{
    printf("webtts_send_text: %s\n", text);
    g_pcm_buffer_index = 0;

    client c;

    // 开启基本日志记录（可选）
    if (g_enable_debug)
    {
        c.set_access_channels(websocketpp::log::alevel::all);
        c.set_error_channels(websocketpp::log::elevel::all);
        c.clear_access_channels(websocketpp::log::alevel::frame_payload); // 避免打印二进制数据
    }
    else
    {
        c.clear_access_channels(websocketpp::log::alevel::frame_payload); // 避免打印二进制数据
        c.set_access_channels(websocketpp::log::alevel::connect |
            websocketpp::log::alevel::disconnect |
            websocketpp::log::alevel::fail);
    }

    c.init_asio();
    c.set_tls_init_handler(bind(&on_tls_init));

    // 添加失败处理器
    c.set_fail_handler([&](websocketpp::connection_hdl hdl) {
        client::connection_ptr con = c.get_con_from_hdl(hdl);
        std::string response_body = con->get_response_msg();
        std::cout << "连接失败，原因: "
                  << con->get_ec().message()
                  << " (状态码: "
                  << con->get_response_code()
                  << ")"
                  << ",message:"
                  << response_body.c_str()
                  << std::endl;
    });

    // 修改消息处理器以捕获更多信息
    c.set_message_handler([&](websocketpp::connection_hdl hdl, client::message_ptr msg) {
        if (msg->get_opcode() == websocketpp::frame::opcode::text) {
            try {
                json response = json::parse(msg->get_payload());
                if (response.contains("data") && response["data"].contains("audio")) {
                    string encoded_audio = response["data"]["audio"].get<string>();
                    string decoded_audio = base64_decode(encoded_audio);
                    int status = response["data"]["status"].get<int>();

                    _encode_pcm_data(decoded_audio.data(), decoded_audio.size(),status == 2);
                }
            } catch (const json::parse_error& e) {
                std::cerr << "JSON 解析错误: " << e.what() << std::endl;
            }
        }
    });

    websocketpp::lib::error_code ec;

    string date_str = get_rfc1123_time();
    string auth_str = get_auth_str();

    // 对参数进行 URL 编码
    string encoded_host = url_encode(HOST);
    string encoded_date = url_encode(date_str);
    string encoded_auth = url_encode(auth_str);

    if (g_enable_debug)
    {
        std::cout << "生成日期头: " << date_str << std::endl;
        std::cout << "授权头原始值: " << auth_str << std::endl;
    }

    // 构建带参数的请求 URL
    string surl = "wss://" + HOST + PATH + "?host=" + encoded_host + "&date=" + encoded_date + "&authorization=" + encoded_auth;

    // 打印连接信息
    if (g_enable_debug)
        std::cout << "正在连接至: " << surl << std::endl;

    client::connection_ptr con = c.get_connection(surl, ec);

    if (ec) {
        std::cerr << "创建连接对象失败: " << ec.message() << std::endl;
        return -1;
    }

    // 添加详细握手日志
    con->set_http_handler([&](websocketpp::connection_hdl hdl) {
        client::connection_ptr con = c.get_con_from_hdl(hdl);

    // 新版本 WebSocket++ 获取请求头的方式
    if (g_enable_debug)
    {
        auto const &headers = con->get_request().get_headers();
        for (auto const &header : headers) {
            std::cout << header.first << ": " << header.second << "\n";
        }
        std::cout << std::endl;
    }

    });

    c.connect(con);

    // 在连接打开时发送请求（添加错误处理）
    con->set_open_handler([&](websocketpp::connection_hdl hdl) {
        try {
            client::connection_ptr con = c.get_con_from_hdl(hdl);
            //std::cout << "连接成功，开始发送请求数据..." << std::endl;

            // 保存 WebSocket 连接句柄
            g_ws_connection_hdl = hdl;
            g_ws_client = &c;

            // 打印请求数据
            char audio_param[1024];
            sprintf(audio_param,"audio/L16;rate=%d",g_webtts_config.sample_rate);
            json request = {
                {"common", {
                    {"app_id", g_webtts_config.app_id.c_str()}
                }},
                {"business", {
                    {"aue", "raw"},               // 输出格式
                    {"auf", audio_param}, // 音频参数
                    {"speed",g_webtts_config.speed},//语速
                    {"vcn", g_webtts_config.voice_name.c_str()},           // 发音人
                    {"tte", "UTF8"}               // 文本编码
                }},
                {"data", {
                    {"text", base64_encode(text)}, // 文本需base64
                    {"status", 2}                 // 固定值2表示结束
                }}
            };

            if (g_enable_debug)
                std::cout << "发送的请求JSON:\n" << request.dump(2) << std::endl;

            c.send(hdl, request.dump(), websocketpp::frame::opcode::text, ec);
            if (ec) {
                std::cerr << "发送失败: " << ec.message() << std::endl;
            }
        } catch (const std::exception &e) {
            std::cerr << "打开连接异常: " << e.what() << std::endl;
        }
    });

    try {
        c.run();
    } catch (const std::exception &e) {
        std::cerr << "运行异常: " << e.what() << std::endl;
    }

    return 0;
}