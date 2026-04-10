/// @file      Engine.hpp
/// @author    Pierre Caissial
/// @date      Created on 31/05/23
/// @copyright Copyright (c) Vivoka (vivoka.com)

#pragma once

// Project includes
#include <vsdk/speech-enhancement/Engine.hpp>

namespace Vsdk::SpeechEnhancement::S2c
{
    class Engine final : public Vsdk::SpeechEnhancement::Engine
    {
        friend class Vsdk::details::IEngine;

    private:
        explicit Engine(char const * configPath);
        explicit Engine(std::string const & configPath);
        explicit Engine(nlohmann::json config);
        Engine(nlohmann::json config, std::string const & configPath);

    public:
        ~Engine();

    public:
        auto version() const -> std::string override;

    private:
        void init() override;
        void checkConfig() const override;
        auto makeSpeechEnhancer(std::string name) -> SpeechEnhancer * override;
    };
} // !namespace Vsdk::SpeechEnhancement::S2c
