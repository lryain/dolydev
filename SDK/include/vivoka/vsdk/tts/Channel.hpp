/// @file      Channel.hpp
/// @author    Pierre Caissial
/// @date      Created on 04/12/2020
/// @copyright Copyright (c) Vivoka (vivoka.com)

#pragma once

// Project includes
#include "../audio/Buffer.hpp"
#include "../audio/Pipeline.hpp"
#include "../details/vsdk/StatusReporter.hpp"

// C++ include
#include <memory>
#include <string>

namespace Vsdk { namespace Tts
{
    class Engine;

    enum class ChannelEventCode
    {
        NativeEvent,

        GenerationStarted,
        GenerationEnded,
        ProcessFinished,

        TextRewritten,
        Marker,
        WordMarkerStart,
        WordMarkerEnd,
    };

    enum class ChannelErrorCode
    {
        /// This one is raised when an unmanaged native error is raised.
        NativeError,

        WordMarkerError
    };

    /// Can perform voice synthesis
    class Channel
        : public Audio::ProducerModule
        , public details::StatusReporter<ChannelEventCode, ChannelErrorCode>
    {
        friend class Engine;

    private:
        std::shared_ptr<Engine> _engine; //!< Engine must survive until Channel is destroyed

    private:
        std::string _name, _currentVoice, _source;
        bool _nativeEventsEnabled;

    protected:
        Channel(std::shared_ptr<Engine> engine, std::string name) noexcept;

    public:
        Channel(Channel const &) noexcept = delete;
        Channel(Channel &&) noexcept = default;
        Channel & operator=(Channel const &) noexcept = delete;
        Channel & operator=(Channel &&) noexcept = default;
        virtual ~Channel() noexcept = default;

    public:
        /// Name of this particular Channel instance
        auto name() const -> std::string const &;

        /// Name of the currently activated voice, if any
        /// @return Empty string if no voice has been successfully activated yet
        auto currentVoice() const -> std::string const &;

#ifndef NDEBUG
        /// Tells whether native events can be received in the events callback (@c false by default)
        bool nativeEventsEnabled() const;
#endif

        /// The sample rate used during synthesis operations in Hertz
        virtual int sampleRate() const = 0;

        /// The channel count used during synthesis operations
        virtual int channelCount() const = 0;

    public:
        /// Activates a voice for the all future synthesis operations
        /// @throws Vsdk::Exception if the engine failed to activate the requested voice
        void setCurrentVoice(std::string const & voice);

#ifndef NDEBUG
        /// Sets whether to receive native events in the events callback
        void setNativeEventsEnabled(bool enabled);
#endif

    public:
        /// Prepares the next synthesis from a text string (either raw or containing SSML)
        /// @note    If the channel is already started and a voice has been set then the synthesis
        ///          starts immediately. If it is not started, then a call to @c start() or @c run()
        ///          will start the synthesis
        /// @throws  Vsdk::Exception if @p text is empty or if an attempt to synthesize failed
        /// @returns @c true if a synthesis started right away, else @c false
        bool synthesizeFromText(std::string text);

        /// Prepares the next synthesis from a file (containing either raw text or SSML). The file
        /// is read right away and its content is stored as preparation for the next synthesis.
        /// @note    If the channel is already started and a voice has been set then the synthesis
        ///          starts immediately. If it is not started, then a call to @c start() or @c run()
        ///          will start the synthesis
        /// @throws  Vsdk::Exception if @p file is not a valid file path, if contained text is empty
        ///                          or if an attempt to synthesize failed
        /// @returns @c true if a synthesis started right away, else @c false
        bool synthesizeFromFile(std::string const & path);

    protected:
        virtual void setCurrentVoiceImpl(std::string const & voice) = 0;
        virtual void synthesize(std::string const & text, bool async) = 0;

    protected:
        void runImpl() override;
        void startImpl() override;

    protected:
        void dispatchEvent(Event event);
        void dispatchError(Error error);
    };

    using ChannelPtr = std::shared_ptr<Channel>;
}} // !namespace Vsdk::Tts
