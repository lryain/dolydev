/// @file      SpeechEnhancer.hpp
/// @author    Pierre Caissial
/// @date      Created on 31/05/23
/// @copyright Copyright (c) Vivoka (vivoka.com)

#pragma once

// Project includes
#include "../audio/Pipeline.hpp"

// C++ includes
#include <memory>
#include <string>

namespace Vsdk { namespace SpeechEnhancement
{
    class SpeechEnhancer : public Audio::ModifierModule
    {
        friend class Engine;

    protected:
        explicit SpeechEnhancer(std::string name);

    public:
        ~SpeechEnhancer();

    public:
        auto name() const -> std::string const &;

        /// Number of expected audio channels to process
        virtual int inputChannelCount()  const = 0;

        /// Number of audio channels to expect once processing is done
        virtual int outputChannelCount() const = 0;

    private:
        struct Pimpl;
        std::unique_ptr<Pimpl> _pimpl;
    };

    using SpeechEnhancerPtr = std::shared_ptr<SpeechEnhancer>;
}} // !namespace Vsdk::SpeechEnhancement
