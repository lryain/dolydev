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
#include "play_pcm.h"
#include "alsa/asoundlib.h"
#include "alsa/pcm.h"
#include <algorithm>

static snd_pcm_t *handle;
static snd_pcm_hw_params_t *params;
static snd_pcm_uframes_t negotiated_frames = 0; // ALSA negotiated period size

int init_play_pcm(int sample_rate,int channels,int frame_samples,int bits_per_sample){
    int rc;
    unsigned int val;
    int dir = 0;
	int ret = -1;

    rc = snd_pcm_open(&handle, "default", SND_PCM_STREAM_PLAYBACK, 0);
    if (rc < 0) {
        printf("unable to open PCM device: %s\n",snd_strerror(rc));
        return -1;
    }

    /* alloc hardware params object */
    snd_pcm_hw_params_alloca(&params);

    /* fill it with default values */
    snd_pcm_hw_params_any(handle, params);

    /* interleaved mode */
    snd_pcm_hw_params_set_access(handle, params, SND_PCM_ACCESS_RW_INTERLEAVED);

	/* signed 16 bit little ending format */
    snd_pcm_hw_params_set_format(handle, params, SND_PCM_FORMAT_S16_LE);

    /* two channels (stereo) */
    snd_pcm_hw_params_set_channels(handle, params, channels);

    /* 44100 bits/second sampling rate (CD quality) */
    snd_pcm_hw_params_set_rate_near(handle, params,(unsigned int*)&sample_rate, &dir);
	//printf("pcm rate: val:%d dir:%d.\n",val,dir);

    /* set period size t 40ms frames */
    negotiated_frames = frame_samples;
    rc = snd_pcm_hw_params_set_period_size_near(handle, params, &negotiated_frames, &dir);

    snd_pcm_uframes_t frame_size = frame_samples*2*3;
    snd_pcm_hw_params_set_buffer_size_near(handle, params, &frame_size);

    /* write params to the driver */
    rc = snd_pcm_hw_params(handle, params);
    if (rc < 0) {
        printf("unable to set hw params: %s\n",snd_strerror(rc));
        return -1;
    }
    /* use buffer large enough to hold one period */
    rc = snd_pcm_hw_params_get_period_size(params, &negotiated_frames, &dir);
    printf("play %d,rc = %d, negotiated_frames:%ld dir:%d.\n",__LINE__,rc,negotiated_frames,dir);

    // 获取实际设置的缓冲队列大小
    snd_pcm_uframes_t buffer_size;
    snd_pcm_hw_params_get_buffer_size(params, &buffer_size);
    printf("play get_buffer_size: %u\n", buffer_size);
    return 0;
}

int play_pcm(char *pcm_buffer, int frames_to_write)
{
    // 以块的形式逐次写入，避免一次性写入导致阻塞
    int frames_written = 0;
    int frame_size = 2;  // 16-bit mono = 2 bytes per frame
    char* buffer_ptr = pcm_buffer;
    
    // Try to write in smaller chunks (e.g., up to negotiated_frames or 512 frames) to reduce latency
    int negotiated = negotiated_frames > 0 ? static_cast<int>(negotiated_frames) : 512;
    int chunk_size = std::min(512, negotiated);

    while (frames_to_write > 0) {
        int write_count = (frames_to_write > chunk_size) ? chunk_size : frames_to_write;

        int ret = snd_pcm_writei(handle, buffer_ptr, write_count);
        if (ret == -EPIPE) {
            /* -EPIPE means underrun */
            // fprintf(stderr, "underrun occured, recovering...\n");
            snd_pcm_prepare(handle);
            continue;
        } else if (ret < 0) {
            fprintf(stderr, "[play_pcm] error from writei: %s\n", snd_strerror(ret));
            return ret;
        } else if (ret > 0) {
            frames_written += ret;
            frames_to_write -= ret;
            buffer_ptr += ret * frame_size;
        } else {
            // ret == 0, device busy, retry after short sleep
            usleep(100);
        }
    }
    
    return frames_written;
}

int deinit_play_pcm(){
	snd_pcm_drain(handle);
    snd_pcm_close(handle);
	return 0;
}

int get_playback_period_frames() {
    return static_cast<int>(negotiated_frames);
}