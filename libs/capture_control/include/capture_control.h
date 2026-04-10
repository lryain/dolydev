#pragma once

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Open or create a shared memory capture control region
// name: posix shm name (e.g. "/doly_capture_control")
// create: whether to create if it doesn't exist
// return true on success
bool capture_control_open(const char* name, bool create);

// Close the shared memory mapping
void capture_control_close();

// Increment the playback reference count (used by audio_player)
// Returns the new count
uint32_t capture_control_inc();

// Decrement the playback reference count (used by audio_player)
// Returns the new count
uint32_t capture_control_dec();

// Get the current playback reference count
uint32_t capture_control_count();

// Check if capture should be muted (true if playback count > 0)
bool capture_control_is_muted();

// Simple boolean flag that indicates whether the audio playback subsystem is actively
// playing audio; this is intended for quick, low-overhead "isPlaying" checks from
// audio_frontend to mute/unmute capture. This is separate from the reference-counted
// playback_count behaviour.
// Set the flag: true to indicate playback active, false to indicate not playing.
void capture_control_set_playing(bool playing);
// Query whether playback is considered active.
bool capture_control_is_playing();

#ifdef __cplusplus
}
#endif

