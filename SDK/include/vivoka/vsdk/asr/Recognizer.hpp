/// @file      Recognizer.hpp
/// @author    Pierre Caissial
/// @date      Created on 21/05/2021
/// @copyright Copyright (c) Vivoka (vivoka.com)

#pragma once

// Project includes
#include "../audio/Buffer.hpp"
#include "../audio/Pipeline.hpp"
#include "../details/vsdk/StatusReporter.hpp"

// C++ includes
#include <string>
#include <unordered_set>

namespace Vsdk { namespace Asr
{
    enum class RecognizerEventCode
    {
        RecognizerStarted,      ///< Recognizer started
        RecognizerStopped,      ///< Recognizer stopped
        SpeechDetected,         ///< Recognizer detected speech
        SilenceDetected,        ///< Recognizer detected silence
    };

    enum class RecognizerErrorCode
    {
        UnexpectedError,        ///< Something unexpected happened, check the message for more info
        UnsupportedResultType, ///< Native engine returned an unsupported type of result
        CannotUpdateUptime,    ///< upTime couldn't be updated with the provided audio buffer
        NoModelSet,            ///< Tried to run the recognizer without having set a model prior
    };

    /// ConsumerModule that performs Automatic Speech Recognition
    class Recognizer
        : public Audio::ConsumerModule
        , public details::StatusReporter<RecognizerEventCode, RecognizerErrorCode>
    {
    protected:
        std::string _name;
        double      _upTime;

    protected:
        explicit Recognizer(std::string name) noexcept;

    public:
        /// Name of this particular Recognizer instance
        auto name()     const -> std::string const &;
        /// Amount of milliseconds of audio processed for the current model(s), expressed as @c int
        auto upTime()   const -> int;
        /// Amount of milliseconds of audio processed for the current model(s), expressed as
        /// @c std::chrono::milliseconds
        auto upTimeMs() const -> std::chrono::milliseconds;

    public:
        /// Applies a new recognition model
        /// @param model     Found in the configuration under @c asr.models
        /// @param startTime Time from which the recognizer starts processing audio again. This is
        ///                  usually taken from a previous result, like so:
        ///                  @code setModel(model, hypothesis["end_time"].get<int>()); @endcode
        /// @note  Some implementations do not support the @p startTime parameter and will
        ///        recognize again starting from the moment the model is fully loaded
        virtual void setModel(std::string const & model, int startTime) = 0;

        /// Applies a new recognition model. Equivalent to calling:
        /// @code setModel(model, -1); @endcode
        void setModel(std::string const & model);

        /// Applies multiple recognition models at the same time
        /// @param models    Found in the configuration under @c asr.models
        /// @param startTime Time from which the recognizer starts processing audio again. This is
        ///                  usually taken from a previous result, like so:
        ///                  @code setModel(model, hypothesis["end_time"].get<int>()); @endcode
        /// @note  Some implementations do not support the @p startTime parameter and will
        ///        recognize again starting from the moment the models are fully loaded
        virtual void setModels(std::unordered_set<std::string> const & models, int startTime) = 0;

        /// Applies multiple recognition models at the same time. Equivalent to calling:
        /// @code setModels(models, -1); @endcode
        void setModels(std::unordered_set<std::string> const & models);

    protected:
        virtual void configure(nlohmann::json const &) {}
        /// @return @c true if the buffer has been processed, else @c false
        virtual bool processBuffer(Audio::Buffer const & buffer, bool last) = 0;
        void dispatchEvent(EventCode code, std::string message, std::chrono::milliseconds time);
        void dispatchError(ErrorType type, ErrorCode code, std::string message = std::string());

    protected:
        void setUpTime(double upTime);

    private:
        void process(Audio::Buffer const & buffer, bool last) override final;
        void updateUpTime(Vsdk::Audio::Buffer const & b);
        auto codeToString(RecognizerEventCode code) -> char const *;
        auto codeToString(RecognizerErrorCode code) -> char const *;
    };

    using RecognizerPtr = std::shared_ptr<Recognizer>;
}} // !namespace Vsdk::Asr
