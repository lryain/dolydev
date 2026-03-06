#ifndef __ALSA_PLAY_PCM_H__
#define __ALSA_PLAY_PCM_H__

#include <stdio.h>
#include <string.h>

int init_play_pcm(int sample_rate,int channels,int frame_samples,int bits_per_sample);

// pcm_buf: pointer to PCM interleaved samples (16-bit mono)
// frames: number of frames (samples) to write to ALSA
int play_pcm(char *pcm_buf, int frames);

// Return the ALSA period size (frames) that was negotiated in init_play_pcm.
int get_playback_period_frames();

int deinit_play_pcm();

#endif