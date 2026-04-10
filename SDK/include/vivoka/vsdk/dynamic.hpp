/// @file      dynamic.hpp
/// @author    Pierre Caissial
/// @date      Created on 11/01/2024
/// @copyright Copyright (c) Vivoka (vivoka.com)

#pragma once

// Third-party includes
#include <nlohmann/json.hpp>

#ifndef VSDK_PLUGIN_EXPORT
# define VSDK_PLUGIN_EXPORT
#endif

#define VSDK_PLUGIN_MAKE_ENGINE(techno, sdk, ...) \
    new std::shared_ptr<techno::Engine>(techno::Engine::make<techno::sdk::Engine>(__VA_ARGS__));

enum Technology
{
    Asr,
    Nlu,
    Tts,
    VoiceBiometrics,
    SpeechEnhancement,
};

extern "C"
{
    VSDK_PLUGIN_EXPORT void * vsdk_engine_makeFromConfigFile(Technology   technology,
                                                             char const * path);

    VSDK_PLUGIN_EXPORT void * vsdk_engine_makeFromJson(Technology     technology,
                                                       nlohmann::json config,
                                                       char const *   configPath);
} // !extern "C"
