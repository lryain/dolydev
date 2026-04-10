/// @file      Recognizer.hpp
/// @copyright Copyright (c) Vivoka (vivoka.com)
/// @author    Pierre Caissial
/// @date      Created on 13/03/2020

#pragma once

// Project includes
#include <vsdk-csdk-core/kernel/Resource.hpp>
#include <vsdk-csdk-core/utils/ProxyPtr.hpp>

// Third-party includes
#include <nlohmann/json.hpp>

// C++ includes
#include <chrono>
#include <functional>
#include <string>

#undef ERROR // thanks Windows

namespace Vsdk { namespace details { namespace Csdk
{
    enum class RecognizerEventCode
    {
#if CSDK_VERSION_MAJOR <= 2
        AudiostreamReceived,      ///< recognizer received the audio stream
        SpeechDetected,           ///< recognizer detected speech
        SilenceDetected,          ///< recognizer detected silence
        AudioStopped,             ///< recognizer detected audio stopped
        AudioFinished,            ///< recognizer detected audio finished
        AbnormcondBadsnr,         ///< The signal to noise ratio is too low
        AbnormcondOverload,       ///< The speech level is too loud
        AbnormcondTooquiet,       ///< The speech level is too weak
        AbnormcondNosignal,       ///< No or very low input signal
        ExternalProviderStarted,  ///< external provider started to work
        ExternalProviderFinished, ///< external provider finished processing audio
#else
        AudiostreamReceived,      ///< recognizer received the audio stream
        SpeechDetected,           ///< recognizer detected speech
        SilenceDetected,          ///< recognizer detected silence
        SpeechUnknown,            ///< recognizer does not know if it is speech or silence
        AudioStopped,             ///< recognizer detected audio stopped
        AudioFinished,            ///< recognizer detected audio finished
        AbnormcondBadsnr,         ///< The signal to noise ratio is too low
        AbnormcondOverload,       ///< The speech level is too loud
        AbnormcondTooquiet,       ///< The speech level is too weak
        AbnormcondNosignal,       ///< No or very low input signal
        ExternalProviderStarted,  ///< external provider started to work
        ExternalProviderFinished, ///< external provider finished processing audio
        UserDeleted,              ///< User deletion success notification
        ActiveUser,               ///< Active User get notification
        UserSet                   ///< Active User set success notification
#endif
    };

    struct RecognizerEvent
    {
        RecognizerEventCode       code = RecognizerEventCode::AudioFinished;
        std::string               codeString;
        std::string               message;
        std::chrono::milliseconds timeMarker; ///< Time elapsed since the start of the recognizer
    };

    enum class RecognizerResultType
    {
        Asr,              ///< ASR result
        External,         ///< ASR External result
        Semantic,         ///< A Semantic result
        PostProcessor,    ///< A Post processor result
        ExternalProvider, ///< A result from an external result provider plugin
    };

    struct RecognizerResult
    {
        RecognizerResultType type;
        std::string          typeString;
        nlohmann::json       json;
        bool                 isFinal;
    };

    enum class RecognizerErrorCode
    {
#if CSDK_VERSION_MAJOR <= 2
        AllocationFailure, ///< an error occurred during memory allocation
        ConfigInvalid,     ///< an error occurred while parsing the configuration file
        FileNotFound,      ///< a file could not be found
        FileInvalid,       ///< a file is not a valid stream
        Error,             ///< a generic error occurred
        Fatal,             ///< a FATAL error occurred
        /// @c lh_AudioConfigBuilderBuild called but there was no prior call to set the sampling
        /// frequency or the @c SSEInfoBuffer
        AudioconfigbuilderMissingSamplefrequency,
        /// @c lh_AudioConfigBuilderBuild called but the sampling frequency was set to a different
        /// value than what is specified in the @c SSEInfo
        AudioconfigbuilderIncompatibleSamplefrequency,
        /// @c lh_AudioConfigBuilderSetSSEInfo was called with an invalid @c SSEInfo data buffer
        AudioconfigbuilderInvalidSseDataBuffer,
        /// @c lh_EngineConfigBuilderSetLanguageConfigPool called multiple times
        EngineconfigbuilderLanguageconfigpoolAlreadySet,
        /// @c lh_EngineConfigBuilderSetSpeakerConfigBuilder called multiple times
        EngineconfigbuilderSpeakerconfigbuilderAlreadySet,
        /// an error occurred from the external result provider plugin
        ExternalProviderError,
        /// an error occurred during LUA Runtime
        Sem3streamingconfigbuilderLuaRuntimeError,
        /// the unfold function is missing from the LUA script
        Sem3streamingconfigbuilderMissingUnfoldFunction,
#else
        UnexpectedFailure, ///< a unknown error occurred.
        AllocationFailure, ///< an error occurred during memory allocation.
        ConfigInvalid,     ///< an error occurred while parsing the configuration file.
        FileNotfound,      ///< a file could not be found.
        FileInvalid,       ///< a file is not a valid stream.
        Error,             ///< a generic error occurred
        Fatal,             ///< a FATAL error occurred
        /// @c lh_AudioConfigBuilderBuild called but there was no prior call to set the sampling
        /// frequency or the @c SSEInfoBuffer
        AudioconfigbuilderMissingSamplefrequency,
        /// @c lh_AudioConfigBuilderBuild called but the sampling frequency was set to a different
        /// value than what is specified in the @c SSEInfo
        AudioconfigbuilderIncompatibleSamplefrequency,
        /// @c lh_AudioConfigBuilderSetSSEInfo was called with an invalid @c SSEInfo data buffer
        AudioconfigbuilderInvalidSseDataBuffer,
        /// @c lh_EngineConfigBuilderSetLanguageConfigPool called multiple times
        EngineconfigbuilderLanguageconfigpoolAlreadySet,
        /// @c lh_EngineConfigBuilderSetSpeakerConfigBuilder called multiple times
        EngineconfigbuilderSpeakerconfigbuilderAlreadySet,
        /// an error occurred from the external result provider plugin
        ExternalProviderError,
        /// an error occurred during LUA Runtime
        Sem3streamingconfigbuilderLuaRuntimeError,
        /// the unfold function is missing from the LUA script
        Sem3streamingconfigbuilderMissingUnfoldFunction,
        UserSetFail,    ///< setActiveUser API call fail
        UserDeleteFail, ///< deleteUser API call fail
#endif
    };

    enum class RecognizerErrorType { Error, Warning };

    struct RecognizerError
    {
        RecognizerErrorType type;
        RecognizerErrorCode code;
        std::string         codeString;
        std::string         message;
    };

    class AsrManager;

    /// @brief  Perform recognition on provided audio data through scenarios
    ///         User can subscribe to events, results and errors/warnings with callbacks
    class Recognizer : public IResource
    {
    public:
        using ResultCallback = std::function<void(RecognizerResult const &)>;
        using EventCallback  = std::function<void(RecognizerEvent  const &)>;
        using ErrorCallback  = std::function<void(RecognizerError  const &)>;

    private:
        std::unique_ptr<Utils::ProxyPtr<Recognizer>> _this;
        Utils::Pimpl<IResource>                      _listener;
        std::string                                  _name;
        bool                                         _isRunning = false;

    private:
        ResultCallback _resultCallback;
        EventCallback  _eventCallback;
        ErrorCallback  _errorCallback;

    public:
        Recognizer(std::string name, AsrManager const & asrManager);
        Recognizer(Recognizer &&) noexcept;
        ~Recognizer();

    public:
        /// @brief  Start analyzing audio data in the background. Does nothing if already started
        /// @note   This call is non-blocking
        void start();
        /// @brief  Stops the background recognition
        /// @note   Does nothing if already stopped or not started yet
        void stop();

    public:
        /// @brief      Install a callback invoked when receiving recognition results
        /// @note       Only one callback can be set at a time. Calling this again will replace
        ///             the previous callback
        /// @warning    @p callback will get invoked on the recognition thread, do @b not try
        ///             to install application or modify the recognizer in this thread!
        void subscribe(ResultCallback callback);
        /// @brief      Install a callback invoked when receiving recognition events
        /// @note       Only one callback can be set at a time. Calling this again will replace
        ///             the previous callback
        /// @warning    @p callback will get invoked on the recognition thread, do @b not try
        ///             to install application or modify the recognizer in this thread!
        void subscribe(EventCallback callback);
        /// @brief      Install a callback invoked when receiving recognition errors/warnings
        /// @note       Only one callback can be set at a time. Calling this again will replace
        ///             the previous callback
        /// @warning    @p callback will get invoked on the recognition thread, do @b not try
        ///             to install application or modify the recognizer in this thread!
        void subscribe(ErrorCallback callback);
        void cancelResultsSubscription();
        void cancelEventsSubscription();
        void cancelErrorsAndWarningsSubscription();

    public:
        auto name() const -> std::string const &;
        bool isRunning() const;
        auto elapsedTimeSinceStart() const -> int;

    public:
        /// @brief Internal function
        void onEvent (RecognizerEvent  const & event);
        /// @brief Internal function
        void onResult(RecognizerResult const & result);
        /// @brief Internal function
        void onError (RecognizerError  const & error);
    };
}}} // !namespace Vsdk::details::Csdk

#define DECLARE(op, type) \
    bool op(Vsdk::details::Csdk::type const & lhs, Vsdk::details::Csdk::type const & rhs)

DECLARE(operator<,  RecognizerResult);
DECLARE(operator>,  RecognizerResult);
DECLARE(operator<=, RecognizerResult);
DECLARE(operator>=, RecognizerResult);
DECLARE(operator==, RecognizerResult);
DECLARE(operator!=, RecognizerResult);

DECLARE(operator<,  RecognizerEvent);
DECLARE(operator>,  RecognizerEvent);
DECLARE(operator<=, RecognizerEvent);
DECLARE(operator>=, RecognizerEvent);
DECLARE(operator==, RecognizerEvent);
DECLARE(operator!=, RecognizerEvent);

DECLARE(operator<,  RecognizerError);
DECLARE(operator>,  RecognizerError);
DECLARE(operator<=, RecognizerError);
DECLARE(operator>=, RecognizerError);
DECLARE(operator==, RecognizerError);
DECLARE(operator!=, RecognizerError);

#undef DECLARE
