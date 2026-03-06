#ifndef __WAKE_WORD_GENERATOR_H__
#define __WAKE_WORD_GENERATOR_H__

typedef void (*wakeup_data_callback)(const unsigned char* data, int length);
void init_wake_word_generator(wakeup_data_callback callback);
int wake_word_file(const char* filename);

#endif
