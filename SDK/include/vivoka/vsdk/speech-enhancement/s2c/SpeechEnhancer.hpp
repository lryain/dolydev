/// @file      SpeechEnhancer.hpp
/// @author    Pierre Caissial
/// @date      Created on 01/06/23
/// @copyright Copyright (c) Vivoka (vivoka.com)

#pragma once

// Project includes
#include <vsdk/speech-enhancement/SpeechEnhancer.hpp>

namespace Vsdk::SpeechEnhancement::S2c
{
    class SpeechEnhancer final : public Vsdk::SpeechEnhancement::SpeechEnhancer
    {
        friend class Engine;

    private: SpeechEnhancer(std::string name, std::string config);
    public: ~SpeechEnhancer();

    public:
        auto inputChannelCount()  const -> int override;
        auto outputChannelCount() const -> int override;

    private:
        void process(Vsdk::Audio::Buffer & buffer, bool last) override;

    private:
        struct Pimpl;
        std::unique_ptr<Pimpl> _pimpl;
    };
} // !namespace Vsdk::SpeechEnhancement::S2c
