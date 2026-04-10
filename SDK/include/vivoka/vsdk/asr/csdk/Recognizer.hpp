/// @file      Recognizer.hpp
/// @author    Pierre Caissial
/// @date      Created on 07/06/2021
/// @copyright Copyright (c) Vivoka (vivoka.com)

#pragma once

// Project includes
#include "../../details/csdk/asr/Recognizer.hpp"

#include <vsdk-csdk-core/audio/IAudioInput.hpp>

// VSDK includes
#include <vsdk/asr/Recognizer.hpp>

// C++ includes
#include <condition_variable>
#include <mutex>

namespace Vsdk { namespace details { namespace Csdk
{
    class AudioManager;
    class AsrManager;
}}}

namespace Vsdk { namespace Asr { namespace Csdk
{
    class Recognizer : public Asr::Recognizer
    {
        friend class Engine;

    private:
        std::condition_variable    _cv;
        std::mutex                 _mutex;
        details::Csdk::IAudioInput _inputModule;
        details::Csdk::Recognizer  _recognizer;
        std::atomic_bool           _waitForLastResult, _atLeastOneModelInstalled;
        bool                       _first;

    private:
        Recognizer(std::string                          name,
                   details::Csdk::AudioManager        & audioManager,
                   details::Csdk::AsrManager    const & asrManager,
                   details::Csdk::Configuration const & config);

    public:
        void setModel(std::string const & model, int startTime) override;
        void setModels(std::unordered_set<std::string> const & models, int startTime) override;

    private:
        bool processBuffer(Audio::Buffer const & buffer, bool last) override;
        void onEvent(details::Csdk::RecognizerEvent const &);
        void onResult(details::Csdk::RecognizerResult const &);
        void onError(details::Csdk::RecognizerError const &);

        auto translateAsrResult(nlohmann::json const & result) const -> nlohmann::json;
        auto translateSemResult(nlohmann::json const & result) const -> nlohmann::json;
    };
}}} // !namespace Vsdk::Asr::Csdk
