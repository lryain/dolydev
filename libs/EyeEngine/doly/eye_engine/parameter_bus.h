#pragma once

#include <atomic>
#include <chrono>
#include <optional>
#include <shared_mutex>
#include <string>

namespace doly::eye_engine {

struct ProceduralProfileSnapshot {
    std::string json_payload;
    std::uint64_t version{0};
    std::string hash;
    std::chrono::system_clock::time_point updated_at;
    std::string source;
};

// Thread-safe store for the active procedural profile.
class ParameterBus {
public:
    ParameterBus();

    // Replaces the profile with a new JSON payload.
    // Returns false when validation fails (error_message populated).
    bool setProfile(const std::string& json_payload,
                    const std::string& source,
                    std::optional<std::string>* error_message = nullptr);

    ProceduralProfileSnapshot snapshot() const;

    std::uint64_t version() const;

private:
    std::string computeHash(const std::string& payload) const;

    mutable std::shared_mutex mutex_;
    ProceduralProfileSnapshot current_;
    std::atomic<std::uint64_t> version_counter_;
};

}  // namespace doly::eye_engine
