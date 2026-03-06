#ifndef __AUDIO_CFG_H__
#define __AUDIO_CFG_H__
#define SAMPLE_RATE 16000
#define BUFFER_SAMPLES 960  //60ms @ 16kHz

// Opus output buffer: 60ms at 24kHz (worst case) = 960 * 24/16 = 1440 samples = ~3KB
// Use 5KB to match xiaozhi's design and leave margin
#define OPUS_OUT_BUFFER_SIZE (1024*5)  // 5KB, matches xiaozhi's OPUS_BUF_SIZE

#define BITS_PER_SAMPLE 16
#define CHANNELS_NUM 1

// Playback buffer: accommodate 60ms at any sample rate (16k-24k)
// 24kHz 60ms = 1440 samples = ~2.8KB per frame
// Ring buffer should hold ~500ms to smooth jitter
#define PLAYBACK_RING_BUFFER_FRAMES (BUFFER_SAMPLES * 8)  // ~480ms @ 16kHz

#define OPUS_ENCODER_BITRATE 30000
#define OPUS_ENCODER_COMPLEXITY 0
#endif // __AUDIO_CFG_H__