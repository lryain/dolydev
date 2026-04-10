/// @file      Utils.hpp
/// @author    BOURAOUI Al-Moez L.A
/// @date      Created on 13/06/2023
/// @copyright Copyright (c) Vivoka (vivoka.com)

#pragma once

// Project inludes
#include "Channel.hpp"

namespace Vsdk { namespace Tts
{
    /// Synthesizes audio data from a SSML or raw text string
    /// @warning This is a @b synchronous call! The longer and/or more complex the text to
    ///          synthesize, the longer it will take for the call to finish.
    auto synthesizeFromText(ChannelPtr const & channel, std::string const & text)
        -> Vsdk::Audio::Buffer;

    /// Synthesizes audio data from a SSML or raw text file
    /// @warning This is a @b synchronous call! The longer and/or more complex the text to
    ///          synthesize, the longer it will take for the call to finish.
    auto synthesizeFromFile(ChannelPtr const & channel, std::string const & path)
        -> Vsdk::Audio::Buffer;
}} // !namespace Vsdk::Tts
