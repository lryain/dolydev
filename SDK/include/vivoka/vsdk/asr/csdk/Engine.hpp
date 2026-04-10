/// @file      Engine.hpp
/// @author    Pierre Caissial
/// @date      Created on 28/02/2021
/// @copyright Copyright (c) Vivoka (vivoka.com)

#pragma once

// Project includes
#include <vsdk-csdk-core/CoreEngine.hpp>

// VSDK includes
#include <vsdk/asr/Engine.hpp>

namespace Vsdk { namespace Asr { namespace Csdk
{
    class Engine final : public Asr::Engine
    {
        friend class Vsdk::details::IEngine;

    private:
        /// Explicit overload to avoid ambiguous calls
        explicit Engine(char const * configPath, bool persistGeneratedConfig = false);

        /// Loads configuration from file located at @p configPath
        explicit Engine(std::string const & configPath, bool persistGeneratedConfig = false);

        /// Explicit overload to avoid implicit conversion from nlohmann::json
        explicit Engine(nlohmann::json config, bool persistGeneratedConfig = false);

        /// Explicit overload to avoid ambiguous calls
        Engine(nlohmann::json config, char const * configPath, bool persistGeneratedConfig = false);

        /// Loads configuration from a JSON object and a "virtual" file path
        /// (needed to write relative paths)
        Engine(nlohmann::json config, std::string const & configPath,
               bool persistGeneratedConfig = false);

    public:
        ~Engine() noexcept;

    public:
        auto coreEngine() -> Vsdk::details::Csdk::CoreEngine &;
        auto version() const -> std::string override;

    private:
        void init() override;
        auto makeRecognizer  (std::string name) -> Vsdk::Asr::Recognizer   * override;
        auto makeDynamicModel(std::string name) -> Vsdk::Asr::DynamicModel * override;

    private:
        struct Pimpl;
        std::unique_ptr<Pimpl> _pimpl;
    };
}}} // !namespace Vsdk::Asr::Csdk
