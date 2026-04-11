#include "audio_player/config.hpp"
#include "audio_player/service.hpp"

#include <csignal>
#include <iostream>
#include <string>

using doly::audio::AudioPlayerConfig;
using doly::audio::AudioPlayerService;
using doly::audio::load_config;

static AudioPlayerService* g_service = nullptr;

namespace {
void handle_signal(int) {
    if (g_service) {
        g_service->stop();
    }
}
} // namespace

int main(int argc, char* argv[]) {
    const std::string config_path = (argc > 1) ? argv[1] : "/home/pi/dolydev/config/audio_player.yaml";

    AudioPlayerConfig config;
    try {
        config = load_config(config_path);
    } catch (const std::exception& e) {
        std::cerr << "[AudioPlayer] Failed to load config: " << e.what() << std::endl;
        return 1;
    }

    AudioPlayerService service(config);
    if (!service.initialize()) {
        std::cerr << "[AudioPlayer] Initialization failed" << std::endl;
        return 1;
    }

    std::signal(SIGINT, handle_signal);
    std::signal(SIGTERM, handle_signal);

    g_service = &service;
    service.run();
    g_service = nullptr;

    return 0;
}