/// @file      Events.hpp
/// @author    BOURAOUI Al-Moez L.A
/// @date      Created on 19/06/2023
/// @copyright Copyright (c) Vivoka (vivoka.com)

#pragma once

// Third-party includes
#include <nlohmann/json.hpp>

namespace Vsdk { namespace Tts { namespace Events
{
    struct Marker
    {
        size_t      index;
        std::string name;
        size_t      posInAudio;
        size_t      posInText;

        Marker() noexcept;
        Marker(size_t index, std::string name, size_t posInAudio, size_t posInText) noexcept;
    };

    /// Serializes a @p marker to JSON format
    void to_json(nlohmann::json & j, Marker const & marker);

    /// Deserializes a @p marker from JSON format
    void from_json(nlohmann::json const & j, Marker & marker);

    struct WordMarker
    {
        size_t      index;
        std::string text;
        std::string word;
        size_t      startPosInAudio;
        size_t      endPosInAudio;
        size_t      startPosInText;
        size_t      endPosInText;

        WordMarker() noexcept;
        WordMarker(size_t index, std::string text, std::string word, size_t startPosInAudio,
                   size_t endPosInAudio, size_t startPosInText, size_t endPosInText) noexcept;
    };

    /// Serializes a @p wordMarker to JSON format
    void to_json(nlohmann::json & j, WordMarker const & wordMarker);

    /// Deserializes a @p wordMarker from JSON format
    void from_json(nlohmann::json const & j, WordMarker & wordMarker);
}}} // !namespace Vsdk::Tts::Events
