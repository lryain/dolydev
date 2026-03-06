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
#include "wake_word_generator.h"

#include <stdio.h>
#include <sys/mman.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <cstdint>
#include <iostream>
#include <pthread.h>
#include <stdbool.h>
#include <stdlib.h>
#include "opus/opus.h"
#include "audio_cfg.h"

static wakeup_data_callback g_wakeup_data_cb = NULL;

static OpusEncoder *opus_encoder = NULL;
static char *encoder_input_buffer = NULL;
static uint8_t *encoder_output_buffer = NULL;

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

void init_wake_word_generator(wakeup_data_callback callback)
{
    g_wakeup_data_cb = callback;
    init_audio_encoder();
}

int wake_word_file(const char* filename)
{
    #define BUFFER_SIZE (16000 * 2 / 100 * 6)
    //send wakeup word audio
    FILE *file;
    char buffer[BUFFER_SIZE];
    size_t bytes_read;

    // 打开文件，以二进制只读模式
    file = fopen(filename, "rb");
    if (file == NULL) {
        perror("failed to open file\n");
        return 1;
    }

    // 循环读取文件内容
    while ((bytes_read = fread(buffer, 1, BUFFER_SIZE, file)) == BUFFER_SIZE) {

        // 编码音频数据并检查错误
        int encoded_size = opus_encode(opus_encoder, (opus_int16 *)buffer,
                    BUFFER_SAMPLES, encoder_output_buffer,
                    OPUS_OUT_BUFFER_SIZE);
        if (encoded_size < 0) {
            printf("Opus encode error: %s\n", opus_strerror(encoded_size));
            continue;
        }
        if (g_wakeup_data_cb)
        {
            // 调用回调函数处理编码后的数据
            g_wakeup_data_cb(encoder_output_buffer, encoded_size);
        }

    }
    // 关闭文件
    fclose(file);
    return 0;
}
