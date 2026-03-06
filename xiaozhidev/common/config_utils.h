#pragma once

#include <cstddef>
#include "json.hpp"

namespace xiaozhi {
namespace config {

constexpr int kDefaultPlaybackSampleRate = 16000;
constexpr int kMinPlaybackSampleRate = 8000;
constexpr int kMaxPlaybackSampleRate = 48000;

// Load playback_sample_rate from CFG_FILE, ensure it exists and is within range.
// Returns a sanitized sample rate and updates the file if it was missing or invalid.
int load_playback_sample_rate();

} // namespace config
} // namespace xiaozhi
