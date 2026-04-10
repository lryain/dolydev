/// @file      Engine.hpp
/// @author    Pierre Caissial
/// @date      Created on 31/05/23
/// @copyright Copyright (c) Vivoka (vivoka.com)

#pragma once

// Project includes
#include "../details/vsdk/IEngine.hpp"

namespace Vsdk { namespace SpeechEnhancement
{
    class SpeechEnhancer;

    class Engine : public Vsdk::details::IEngine
    {
    public:
        struct SpeechEnhancerInfo
        {
            std::string name;
        };

    protected:
        /// @param name       Name of the configuration object in @c vsdk.json
        /// @param configPath Path to a @c vsdk.json formatted file
        Engine(char const * name, std::string const & configPath);

        /// @param name   Name of the configuration object in @c vsdk.json
        /// @param config A @c vsdk.json formatted JSON configuration object
        Engine(char const * name, nlohmann::json config);

        /// Specialization used to avoid overload collisions,
        /// forwards to <tt>Engine(char const*, std::string const &);</tt>
        /// @param name       Name of the configuration object in @c vsdk.json
        /// @param configPath Path to a @c vsdk.json formatted file
        Engine(char const * name, char const * configPath);

    public:
        ~Engine();

    public:
        /// Gets or constructs a shared SpeechEnhancement::Engine instance of type @c T with @p args
        /// @note If an engine of this type exists, a shared instance will be returned instead.
        template<class T, typename... Args>
        static auto make(Args &&... args) -> std::shared_ptr<Engine>
        {
            static_assert(std::is_base_of<Engine, T>::value,
                          "T must be a child of SpeechEnhancement::Engine");
            return IEngine::make<Engine, T>(std::forward<Args>(args)...);
        }

    public:
        auto speechEnhancer(std::string const & name) -> std::shared_ptr<SpeechEnhancer>;

        /// Gets available speech enhancers info
        auto speechEnhancersInfo() const
            -> std::unordered_map<std::string, SpeechEnhancerInfo> const &;

    protected:
        virtual auto makeSpeechEnhancer(std::string name) -> SpeechEnhancer * = 0;

        /// @note If @c info.name.empty() then removes entry
        void setEnhancerInfo(std::string const & name, SpeechEnhancerInfo info);

    private:
        struct Pimpl;
        Pimpl * _pimpl;
    };

    using EnginePtr = std::shared_ptr<Engine>;
}} // !namespace Vsdk::SpeechEnhancement
