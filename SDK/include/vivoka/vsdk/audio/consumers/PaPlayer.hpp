/// @file      PaPlayer.hpp
/// @author    Pierre Caissial
/// @date      Created on 11/05/2023
/// @copyright Copyright (c) Vivoka (vivoka.com)

#pragma once

// Project includes
#include "../PaStandalonePlayer.hpp"

// VSDK includes
#include <vsdk/audio/Pipeline.hpp>

namespace Vsdk { namespace Audio { namespace Consumer
{
    class PaPlayer : public Vsdk::Audio::ConsumerModule
    {
    private:
        PaStandalonePlayer _player;

    public:
        using Seconds          = PaStandalonePlayer::Seconds;
        using FinishedCallback = PaStandalonePlayer::FinishedCallback;
        using ProgressCallback = PaStandalonePlayer::ProgressCallback;
        using ErrorCallback    = PaStandalonePlayer::ErrorCallback;
        using State            = PaStandalonePlayer::State;

    public:
        explicit PaPlayer(std::string deviceName = "default", bool asyncCallbacks = false);

    public:
        /// Gets the device name that the next open/write/close cycle will try to open
        /// @note @c "default" means default device, not the device named "default"
        auto deviceName() const -> std::string;

        /// Sets the device name for the next opening operation. Setting it to @c "default"
        /// shall attempts to open the default output device
        /// @note This doesn't close/reopen the current device
        void setDevice(std::string const & name);

        /// Set the @p callback to be called when finishes playing audio
        void setFinishedCallback(FinishedCallback callback);

        /// Set the @p callback to be called when playing progress change
        void setProgressCallback(ProgressCallback callback);

        /// Set the @p callback to be called when error occured when playing audio
        void setErrorCallback(ErrorCallback callback);

        /// Set the audio playing volume
        /// @param value The volume value must be between 0.0 and 1.0
        void setVolume(double value);

        /// Get player volume
        auto volume() const -> double;

        /// Return true if is playing audio
        auto isPlaying() const -> bool;

        /// Pauses the player
        void pause();

        /// Resumes the player
        void resume();

    protected:
        void process(Vsdk::Audio::Buffer const & buf, bool last) override;
    };
}}} // !namespace Vsdk::Audio::Consumer
