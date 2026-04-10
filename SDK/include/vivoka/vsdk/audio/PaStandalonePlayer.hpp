/// @file      PaStandalonePlayer.hpp
/// @author    Pierre Caissial
/// @date      Created on 02/09/2021
/// @copyright Copyright (c) Vivoka (vivoka.com)

#pragma once

// Project includes
#include "../utils/EventWorker.hpp"

// VSDK includes
#include <vsdk/audio/Buffer.hpp>

// C++ includes
#include <atomic>
#include <chrono>
#include <vector>

namespace Vsdk { namespace Audio
{
    class PaStandalonePlayer
    {
    public:
        using Seconds          = std::chrono::duration<double, std::chrono::seconds::period>;
        using FinishedCallback = std::function<void()>;
        using ProgressCallback = std::function<void(Seconds, Seconds)>;
        using ErrorCallback    = std::function<void(std::string const &)>;

        enum class State
        {
            Stopped,
            Idle,
            Playing,
            Paused,
        };

    public:
        /// Creates a standalone audio player
        /// @p deviceName     Audio output device name ('default' to select default audio output)
        /// @p asyncCallbacks When enabled callbacks will be executed in another thread to avoid
        ///                   output under flow (inserted gap because the callback is using
        ///                   too much CPU time)
        explicit PaStandalonePlayer(std::string const & deviceName = "default",
                                    bool asyncCallbacks = false);
        ~PaStandalonePlayer();

    public:
        /// Play @p buffer on @p deviceName and wait till finish
        void play(Vsdk::Audio::Buffer const & buffer, std::string const & deviceName = "default");

        /// Play @p data with @p channelCount and @p sampleRate on @p deviceName and wait till finish
        void play(std::vector<int16_t> const & data, int sampleRate, int channelCount,
                  std::string const & deviceName = "default");

        /// Start playing @p buffer in async mode
        void start(Vsdk::Audio::Buffer buffer, bool last = true);

        /// Start playing @p buffer in async mode
        /// @param buffer The audio buffer to play. Must exist during the playing life cycle.
        ///               You have to delete the buffer by yourself after stoping the player.
        void start(Vsdk::Audio::Buffer const * buffer);

        /// Start playing @p data with @p channelCount and @p sampleRate in async mode
        /// When @p last equal to true the player will stop after playing @p buffer
        void start(std::vector<int16_t> data, int sampleRate, int channelCount, bool last = true);

        /// Append @p buffer to the local buffer after @fn start
        /// When @p last equal to true the player will stop after playing @p buffer
        void append(Vsdk::Audio::Buffer const & buffer, bool last = true);

        /// Pause the played audio
        void pause();

        /// Restart the pause audio
        void resume();

        /// Stop the played audio
        void stop();

        /// Wait until audio finish playing
        /// @param timeout The time limit, to wait for audio finish playing. If this parameter is
        ///                omitted, a value of 0 is used, meaning wait until audio finish playing.
        bool wait(std::chrono::milliseconds timeout = std::chrono::milliseconds(0));

        /// Set the @p callback to be called when finishes playing audio
        /// @note It's not possible to use PaStandalonePlayer methods in this callback
        void setFinishedCallback(FinishedCallback callback);

        /// Set the @p callback to be called when playing progress change
        /// @note It's not possible to use PaStandalonePlayer methods in this callback
        void setProgressCallback(ProgressCallback callback);

        /// Set the @p callback to be called when error occured when playing audio
        /// @note It's not possible to use PaStandalonePlayer methods in this callback
        void setErrorCallback(ErrorCallback callback);

        /// Set the audio playing volume
        /// @param value The volume value must be between 0.0 and 1.0
        void setVolume(double value);

        /// Set the audio output device
        /// @param name the audio output device name ('default' to select default audio output)
        void setDevice(std::string const & name);

        /// Seek to sample position
        void seek(size_t samplePosition);

        /// Seek to time
        void seek(Seconds seconds);

        /// Get player volume
        auto volume() const -> double;

        /// Get current sample position
        auto position() const -> size_t;

        /// Get current time in seconds
        auto time() const -> Seconds;

        /// Get audio output device name
        auto deviceName() const -> std::string;

        /// Get player state
        auto state() const -> State;

        /// Return true if is playing audio
        auto isPlaying() const -> bool;

    private:
        struct Pimpl;
        std::unique_ptr<Pimpl> _pimpl;
    };
}} // !namespace Vsdk::Audio
