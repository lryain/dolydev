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
#include "acap.h"
#include "capture_pcm.h"
#include "play_pcm.h"
#include <pthread.h>
#include <stdio.h>
#include <unistd.h>
#include <stdint.h>
#include <cstdlib>
#include <sys/time.h>
#include <vector>
#include <cstring>
#include <algorithm>
#include <queue>
#include <mutex>
#include <condition_variable>
#include "opus/opus.h"
#include "play_pcm.h"
#include "audio_cfg.h"
#include "webrtc_audio_improvement.h"
#include "ipc_udp.h"
#include "config_utils.h"

extern p_ipc_endpoint_t g_ipc_wakeup_detect_audio_ep;
extern int g_wakeup_word_start;

#define ENABLE_DEBUG_AUDIO 0
#define ENABLE_AUDIO_ENHANCEMENT 0

#if ENABLE_DEBUG_AUDIO
#define AUDIO_DEBUG(fmt, ...) printf(fmt, ##__VA_ARGS__)
#else
#define AUDIO_DEBUG(fmt, ...) ((void)0)
#endif
static pthread_t capture_thread;
static bool g_capture_thread_running = false;
static int stop_threads = 0; // 用于控制线程的停止
static int g_capture_period_frames = BUFFER_SAMPLES;
static size_t g_capture_period_bytes = BUFFER_SAMPLES * BITS_PER_SAMPLE / 8;

static std::vector<int16_t> g_capture_period_buffer;
static std::vector<int16_t> g_pcm_chunk_buffer;
static std::vector<int16_t> g_pcm_enhanced_buffer;
static std::vector<int16_t> g_pcm_reservoir;

// 播放缓冲队列（解耦接收与播放）
static std::queue<std::vector<int16_t>> g_playback_buffer_queue;
static pthread_mutex_t g_playback_queue_mutex = PTHREAD_MUTEX_INITIALIZER;
static bool g_playback_thread_running = false;
static pthread_t playback_thread;

// 环形缓冲（替代队列，平滑播放）
static std::vector<int16_t> g_playback_ring_buffer;
static size_t g_ring_capacity = PLAYBACK_RING_BUFFER_FRAMES;  // Defined in audio_cfg.h
static size_t g_ring_read_pos = 0;
static size_t g_ring_write_pos = 0;
static size_t g_ring_count = 0;  // 当前缓冲样本数
static pthread_mutex_t g_ring_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t g_ring_cond = PTHREAD_COND_INITIALIZER;

static opus_int16 *output_buffer = NULL;
static size_t output_buffer_capacity = BUFFER_SAMPLES * 2;  // max samples
static OpusDecoder *opus_decoder = NULL;
static int decoder_sample_rate = SAMPLE_RATE;

static int g_playback_sample_rate = SAMPLE_RATE;
static size_t g_playback_frame_samples = BUFFER_SAMPLES;
static const int kPlaybackFrameDurationMs = 60;

static OpusEncoder *opus_encoder = NULL;
static uint8_t *encoder_output_buffer = NULL;
static acap_opus_data_callback g_opus_data_callback = NULL;

static FILE* pcm_speaker_file = NULL;
static FILE* pcm_mic_file = NULL;
static FILE* pcm_out_file = NULL;
static int init_file()
{
    pcm_speaker_file = fopen("speaker.pcm", "wb");
    if (!pcm_speaker_file) {
        printf("Failed to open speaker PCM file");
        return -1;
    }

    pcm_mic_file = fopen("mic.pcm", "wb");
    if (!pcm_mic_file) {
        printf("Failed to open mic PCM file");
        return -1;
    }

    pcm_out_file = fopen("out.pcm", "wb");
    if (!pcm_out_file) {
        printf("Failed to open out PCM file");
        return -1;
    }

    return 0;
}

static int deinit_file()
{
    if (pcm_speaker_file)
    {
        fclose(pcm_speaker_file);
        pcm_speaker_file = NULL;
    }

    if (pcm_mic_file)
    {
        fclose(pcm_mic_file);
        pcm_mic_file = NULL;
    }

    if (pcm_out_file)
    {
        fclose(pcm_out_file);
        pcm_out_file = NULL;
    }

    return 0;
}

static size_t calculate_playback_frame_samples(int sample_rate) {
    if (sample_rate <= 0) {
        return BUFFER_SAMPLES;
    }
    size_t samples = static_cast<size_t>(sample_rate) * kPlaybackFrameDurationMs / 1000;
    return std::max<size_t>(samples, 1);
}

static void stop_playback_thread();
static void start_playback_thread();

static size_t calculate_ring_capacity(int sample_rate) {
    size_t baseline = PLAYBACK_RING_BUFFER_FRAMES;
    size_t half_second = static_cast<size_t>(std::max(sample_rate, 0)) / 2;
    return std::max(baseline, half_second);
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
    encoder_output_buffer = (uint8_t *)malloc(OPUS_OUT_BUFFER_SIZE);
    printf("%s\n", "init_audio_encoder");
}

static void init_audio_decoder(int sample_rate) {
    if (sample_rate <= 0) {
        sample_rate = SAMPLE_RATE;
    }

    if (opus_decoder != NULL) {
        opus_decoder_destroy(opus_decoder);
        opus_decoder = NULL;
    }

    int decoder_error = 0;
    opus_decoder = opus_decoder_create(sample_rate, 1, &decoder_error);
    if (decoder_error != OPUS_OK) {
        printf("Failed to create OPUS decoder at %d Hz: %s\n", sample_rate, opus_strerror(decoder_error));
        opus_decoder = NULL;
        return;
    }

    decoder_sample_rate = sample_rate;

    size_t max_frame_samples = std::max<size_t>(5760, calculate_playback_frame_samples(sample_rate) * 2);
    if (output_buffer != NULL) {
        free(output_buffer);
        output_buffer = NULL;
    }
    output_buffer_capacity = max_frame_samples;
    output_buffer = (opus_int16 *)malloc(output_buffer_capacity * sizeof(opus_int16));
    printf("init_audio_decoder: sample_rate=%d, buffer_capacity=%zu\n", sample_rate, output_buffer_capacity);
}

// Reconfigure decoder for a different sample rate (called when server specifies playback rate)
int acap_set_decoder_sample_rate(int sample_rate) {
    printf("[acap] Reconfiguring decoder from %d Hz to %d Hz\n", decoder_sample_rate, sample_rate);

    if (sample_rate <= 0) {
        printf("[acap] Invalid decoder sample rate: %d\n", sample_rate);
        return -1;
    }

    init_audio_decoder(sample_rate);
    if (opus_decoder == NULL) {
        return -1;
    }

    size_t new_frame_samples = calculate_playback_frame_samples(sample_rate);
    size_t new_ring_capacity = calculate_ring_capacity(sample_rate);

    stop_playback_thread();
    deinit_play_pcm();

    pthread_mutex_lock(&g_ring_mutex);
    g_playback_sample_rate = sample_rate;
    g_playback_frame_samples = new_frame_samples;
    g_ring_read_pos = 0;
    g_ring_write_pos = 0;
    g_ring_count = 0;
    if (new_ring_capacity != g_ring_capacity) {
        g_playback_ring_buffer.assign(new_ring_capacity, 0);
        g_ring_capacity = new_ring_capacity;
        printf("[acap] Resized playback ring buffer to %zu samples\n", g_ring_capacity);
    } else if (!g_playback_ring_buffer.empty()) {
        std::fill(g_playback_ring_buffer.begin(), g_playback_ring_buffer.end(), 0);
    }
    pthread_mutex_unlock(&g_ring_mutex);

    if (init_play_pcm(sample_rate, CHANNELS_NUM, static_cast<int>(new_frame_samples), BITS_PER_SAMPLE) != 0) {
        printf("[acap] Failed to reinitialise ALSA playback at %d Hz\n", sample_rate);
        return -1;
    }

    start_playback_thread();
    if (!g_playback_thread_running) {
        printf("[acap] Failed to restart playback thread after reconfiguration\n");
        return -1;
    }

    printf("[acap] Decoder and playback path updated for %d Hz\n", sample_rate);
    return 0;
}

// debug helper to print current thread id
static void print_thread_info(const char *ctx) {
    pid_t tid = getpid();
    printf("[acap] %s pid=%d\n", ctx, tid);
}

int play_opus_stream(const unsigned char* data, int size)
{
    static int play_count = 0;
    play_count++;
    AUDIO_DEBUG("[acap] play_opus_stream called: size=%d, count=%d\n", size, play_count);

    // Opus frames: For any sample rate, a standard frame is 960 samples (60ms at 16kHz)
    // At 24kHz, 960 samples is still 40ms
    // But Opus can handle various output sizes
    int max_out_frames = 5760;  // Max samples Opus can output per call

    AUDIO_DEBUG("[acap] About to decode: decoder_sample_rate=%d, max_out_frames=%d, output_buffer_capacity=%zu\n",
                decoder_sample_rate, max_out_frames, output_buffer_capacity);

    int decoded_size = opus_decode(opus_decoder, data, size, output_buffer, max_out_frames, 0);
    AUDIO_DEBUG("[acap] opus_decode returned: decoded_size=%d\n", decoded_size);
    
    if (decoded_size <= 0) {
        printf("[acap] Opus decode error: %d (sample_rate=%d, max_frames=%d, packet_size=%d bytes)\n", 
               decoded_size, decoder_sample_rate, max_out_frames, size);
        // Print first few bytes of packet for debugging
        printf("[acap] Packet hex: ");
        for (int i = 0; i < (size < 16 ? size : 16); i++) {
            printf("%02x ", data[i]);
        }
        printf("\n");
        return -1;
    }

    if (pcm_speaker_file != NULL) {
        fwrite(output_buffer, 1, decoded_size * 2, pcm_speaker_file);
    }

    AUDIO_DEBUG("[acap] Acquired ring mutex...\n");
    // Write decoded PCM to ring buffer (non-blocking, drop oldest if full)
    pthread_mutex_lock(&g_ring_mutex);
    AUDIO_DEBUG("[acap] Ring mutex locked, writing %d samples\n", decoded_size);
    
    for (int i = 0; i < decoded_size; ++i) {
        if (g_ring_count == g_ring_capacity) {
            // Ring buffer full, drop oldest sample
            g_ring_read_pos = (g_ring_read_pos + 1) % g_ring_capacity;
            g_ring_count--;
        }
        g_playback_ring_buffer[g_ring_write_pos] = output_buffer[i];
        g_ring_write_pos = (g_ring_write_pos + 1) % g_ring_capacity;
        g_ring_count++;
    }
    
    AUDIO_DEBUG("[acap] Ring buffer updated, ring_count=%zu, signaling condition\n", g_ring_count);
    pthread_cond_signal(&g_ring_cond);
    pthread_mutex_unlock(&g_ring_mutex);
    AUDIO_DEBUG("[acap] play_opus_stream finished\n");

#if ENABLE_AUDIO_ENHANCEMENT
    webrtc_process_reference_audio((char*)output_buffer, decoded_size * 2);
#endif

    return 0;
}

// 播放线程函数 - 后台持续从环形缓冲取数据播放
static void* playback_thread_func(void* arg) {
    print_thread_info("playback_thread_func start");
    std::vector<int16_t> playback_buffer;
    int playback_count = 0;

    while (g_playback_thread_running) {
        // Use negotiated ALSA period frames where possible to match device expectations
        int alsa_frames = get_playback_period_frames();
        size_t target_frames = g_playback_frame_samples;
        if (alsa_frames > 0) {
            target_frames = static_cast<size_t>(alsa_frames);
        }
        if (target_frames == 0) {
            target_frames = BUFFER_SAMPLES;
        }

        if (playback_buffer.size() != target_frames) {
            playback_buffer.assign(target_frames, 0);
        }

        size_t pb_pos = 0;

        while (pb_pos < target_frames && g_playback_thread_running) {
            pthread_mutex_lock(&g_ring_mutex);

            size_t toread = std::min(target_frames - pb_pos, g_ring_count);
            if (toread == 0) {
                struct timespec timeout;
                clock_gettime(CLOCK_REALTIME, &timeout);
                timeout.tv_nsec += 50 * 1000 * 1000;
                if (timeout.tv_nsec >= 1000 * 1000 * 1000) {
                    timeout.tv_sec += 1;
                    timeout.tv_nsec -= 1000 * 1000 * 1000;
                }
                pthread_cond_timedwait(&g_ring_cond, &g_ring_mutex, &timeout);
                pthread_mutex_unlock(&g_ring_mutex);
                continue;
            }

            for (size_t i = 0; i < toread; ++i) {
                playback_buffer[pb_pos + i] = g_playback_ring_buffer[g_ring_read_pos];
                g_ring_read_pos = (g_ring_read_pos + 1) % g_ring_capacity;
            }
            pb_pos += toread;
            g_ring_count -= toread;

            if ((++playback_count % 50) == 0) {
                AUDIO_DEBUG("[acap] playback: pb_pos=%zu, target_frames=%zu, ring_count=%zu\n",
                            pb_pos, target_frames, g_ring_count);
            }
            pthread_mutex_unlock(&g_ring_mutex);
        }

        if (pb_pos == target_frames || (!g_playback_thread_running && pb_pos > 0)) {
            if (pb_pos < target_frames) {
                std::fill(playback_buffer.begin() + pb_pos, playback_buffer.end(), 0);
            }
            // write exactly pb_pos frames (which should equal target_frames)
            play_pcm(reinterpret_cast<char*>(playback_buffer.data()), static_cast<int>(target_frames));
        }
    }

    AUDIO_DEBUG("[acap] playback_thread_func end\n");
    return NULL;
}

static void stop_playback_thread() {
    if (g_playback_thread_running) {
        g_playback_thread_running = false;
        pthread_cond_signal(&g_ring_cond);
        if (playback_thread != 0) {
            pthread_join(playback_thread, NULL);
            playback_thread = 0;
        }
    }
}

static void start_playback_thread() {
    if (g_playback_thread_running) {
        return;
    }
    g_playback_thread_running = true;
    if (pthread_create(&playback_thread, NULL, playback_thread_func, NULL) != 0) {
        perror("Failed to create playback thread");
        g_playback_thread_running = false;
    }
}
static void* capture_thread_func(void* arg) {
    int iter = 0;
    print_thread_info("capture_thread_func start");
    while (!stop_threads) {
        iter++;
        int cap_ret = capture_pcm(reinterpret_cast<char*>(g_capture_period_buffer.data()));
        if (cap_ret != 0) {
            if ((iter % 100) == 0) {
                printf("[acap] capture_pcm returned %d at iter=%d\n", cap_ret, iter);
            }
            usleep(1000);
            continue;
        }

        if (g_wakeup_word_start) {
            if ((iter % 100) == 0) {
                printf("[acap] g_wakeup_word_start is set, dropping audio at iter=%d\n", iter);
            }
            g_pcm_reservoir.clear();
            continue;
        }

        // 累积 ALSA 实际采集到的数据
        g_pcm_reservoir.insert(g_pcm_reservoir.end(),
                               g_capture_period_buffer.begin(),
                               g_capture_period_buffer.end());

        // 防止缓冲无限增长
        if (g_pcm_reservoir.size() > BUFFER_SAMPLES * 16) {
            size_t drop_frames = g_pcm_reservoir.size() - BUFFER_SAMPLES * 16;
            g_pcm_reservoir.erase(g_pcm_reservoir.begin(), g_pcm_reservoir.begin() + drop_frames);
            printf("[acap] reservoir overflow, dropped %zu frames\n", drop_frames);
        }

        while (g_pcm_reservoir.size() >= BUFFER_SAMPLES) {
            std::copy(g_pcm_reservoir.begin(),
                      g_pcm_reservoir.begin() + BUFFER_SAMPLES,
                      g_pcm_chunk_buffer.begin());

            if (g_ipc_wakeup_detect_audio_ep) {
                g_ipc_wakeup_detect_audio_ep->send(g_ipc_wakeup_detect_audio_ep,
                                                   reinterpret_cast<char*>(g_pcm_chunk_buffer.data()),
                                                   BUFFER_SAMPLES * sizeof(int16_t));
            }

            if (pcm_mic_file != NULL) {
                fwrite(g_pcm_chunk_buffer.data(), sizeof(int16_t), BUFFER_SAMPLES, pcm_mic_file);
            }

            int ret = -1;
#if ENABLE_AUDIO_ENHANCEMENT
            ret = webrtc_enhance_audio_quality(reinterpret_cast<char*>(g_pcm_chunk_buffer.data()),
                                               g_pcm_enhanced_buffer.data());
#endif

            const opus_int16 *encode_source = nullptr;
            if (ret == 0) {
                if (pcm_out_file != NULL) {
                    fwrite(g_pcm_enhanced_buffer.data(), sizeof(int16_t), BUFFER_SAMPLES, pcm_out_file);
                }
                encode_source = g_pcm_enhanced_buffer.data();
            } else {
                if (pcm_out_file != NULL) {
                    fwrite(g_pcm_chunk_buffer.data(), sizeof(int16_t), BUFFER_SAMPLES, pcm_out_file);
                }
                encode_source = g_pcm_chunk_buffer.data();
            }

            int encoded_size = opus_encode(opus_encoder,
                                           encode_source,
                                           BUFFER_SAMPLES,
                                           encoder_output_buffer,
                                           OPUS_OUT_BUFFER_SIZE);
            if (encoded_size < 0) {
                // printf("[acap] Opus encode error: %s\n", opus_strerror(encoded_size));
                break;
            }

            if (g_opus_data_callback) {
                g_opus_data_callback(encoder_output_buffer, encoded_size);
                if ((iter % 50) == 0) {
                    // printf("[acap] sent opus frame, encoded_size=%d, iter=%d\n", encoded_size, iter);
                }
            }

            g_pcm_reservoir.erase(g_pcm_reservoir.begin(),
                                   g_pcm_reservoir.begin() + BUFFER_SAMPLES);
        }
    }

    printf("capture_thread_func end (stop_threads=%d)\n", stop_threads);
    return NULL;
}

void acap_init(acap_opus_data_callback callback) {

    g_opus_data_callback = callback;
#if ENABLE_AUDIO_ENHANCEMENT
    webrtc_audio_quality_init(SAMPLE_RATE,CHANNELS_NUM);
#endif

    int configured_rate = xiaozhi::config::load_playback_sample_rate();
    if (configured_rate <= 0) {
        configured_rate = SAMPLE_RATE;
    }
    g_playback_sample_rate = configured_rate;
    g_playback_frame_samples = calculate_playback_frame_samples(g_playback_sample_rate);
    g_ring_capacity = calculate_ring_capacity(g_playback_sample_rate);

    printf("[acap] playback sample_rate=%d Hz, frame_samples=%zu, ring_capacity=%zu\n",
           g_playback_sample_rate, g_playback_frame_samples, g_ring_capacity);

    if (init_capture_pcm(SAMPLE_RATE, CHANNELS_NUM, BUFFER_SAMPLES, BITS_PER_SAMPLE) != 0) {
        printf("[acap] init_capture_pcm failed\n");
        return;
    }

    g_capture_period_frames = capture_pcm_get_frame_samples();
    if (g_capture_period_frames <= 0) {
        g_capture_period_frames = BUFFER_SAMPLES;
    }
    g_capture_period_bytes = g_capture_period_frames * sizeof(int16_t);
    printf("[acap] ALSA period frames=%d (%zu bytes)\n", g_capture_period_frames, g_capture_period_bytes);

    g_capture_period_buffer.assign(g_capture_period_frames, 0);
    g_pcm_chunk_buffer.assign(BUFFER_SAMPLES, 0);
    g_pcm_enhanced_buffer.assign(BUFFER_SAMPLES, 0);
    g_pcm_reservoir.clear();
    g_pcm_reservoir.reserve(BUFFER_SAMPLES * 4);

    // Initialize ring buffer
    g_playback_ring_buffer.assign(g_ring_capacity, 0);
    g_ring_read_pos = 0;
    g_ring_write_pos = 0;
    g_ring_count = 0;

    if (init_play_pcm(g_playback_sample_rate, CHANNELS_NUM, static_cast<int>(g_playback_frame_samples), BITS_PER_SAMPLE) != 0) {
        printf("[acap] init_play_pcm failed for sample_rate=%d\n", g_playback_sample_rate);
        return;
    }
    init_audio_encoder();
    init_audio_decoder(g_playback_sample_rate);
    if (opus_decoder == NULL) {
        printf("[acap] init_audio_decoder failed\n");
        return;
    }
#if ENABLE_DEBUG_AUDIO
    init_file();
#endif

    // Start playback thread for audio buffering
    start_playback_thread();
}

void acap_deinit() {
    stop_playback_thread();

    deinit_capture_pcm();
    deinit_play_pcm();
}

void acap_start() {

    stop_threads = 0;
    g_pcm_reservoir.clear();

    // Clear ring buffer
    pthread_mutex_lock(&g_ring_mutex);
    g_ring_read_pos = 0;
    g_ring_write_pos = 0;
    g_ring_count = 0;
    pthread_mutex_unlock(&g_ring_mutex);

    if (g_capture_thread_running) {
        printf("[acap] capture thread already running\n");
        return;
    }

    if (g_capture_period_buffer.empty()) {
        printf("[acap] capture buffer is not initialised\n");
        return;
    }

    // Start playback thread if not already running
    if (!g_playback_thread_running) {
        start_playback_thread();
        if (!g_playback_thread_running) {
            return;
        }
    }

    if (pthread_create(&capture_thread, NULL, capture_thread_func, NULL) != 0) {
        perror("Failed to create capture thread");
        return;
    }
    g_capture_thread_running = true;
}

void acap_stop() {
    stop_threads = 1; // 设置标志位，停止线程
    stop_playback_thread();

    // 等待线程结束
    if (g_capture_thread_running) {
        pthread_join(capture_thread, NULL);
        g_capture_thread_running = false;
    }
}

// 播放指定 PCM 文件的函数
static void acap_play_pcm_file(const char* file_path) {
    FILE* pcm_file = fopen(file_path, "rb");
    if (!pcm_file) {
        perror("Failed to open PCM file");
        return;
    }

    init_play_pcm(SAMPLE_RATE, CHANNELS_NUM, BUFFER_SAMPLES, BITS_PER_SAMPLE);

    char pcm_play_buf[BUFFER_SAMPLES * BITS_PER_SAMPLE / 8];

    while (true) {
        // 从文件读取 PCM 数据
        size_t read_size = fread(pcm_play_buf, 1, sizeof(pcm_play_buf), pcm_file);
        if (read_size < sizeof(pcm_play_buf)) {
            // 如果文件读取完毕，退出循环
            printf("End of PCM file reached\n");
            break;
        }

        // 播放 PCM 数据 (write exactly BUFFER_SAMPLES frames)
        play_pcm(pcm_play_buf, BUFFER_SAMPLES);
    }

    deinit_play_pcm();
    fclose(pcm_file);
}

void set_tts_state(int tts_state)
{
    webrtc_enable_ref_audio(tts_state);
}
