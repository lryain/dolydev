#ifndef ALSA_ACAP_H
#define ALSA_ACAP_H

typedef void (*acap_opus_data_callback)(const unsigned char* data, int length);


void acap_init(acap_opus_data_callback callback);
void acap_deinit();
void acap_start();
void acap_stop();
int acap_set_decoder_sample_rate(int sample_rate);
int  play_opus_stream(const unsigned char* data, int size);
void set_tts_state(int tts_state);
#endif
