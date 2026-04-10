/// @file      Engine.hpp
/// @author    Pierre Caissial
/// @date      Created on 04/12/2020
/// @copyright Copyright (c) Vivoka (vivoka.com)

#pragma once

// Project includes
#include "../details/vsdk/IEngine.hpp"

namespace Vsdk { namespace Tts
{
    using ChannelVoices = std::map<std::string, std::vector<std::string>>;

    class Channel;

    /// Responsible for creation of Channel instances
    class Engine : public Vsdk::details::IEngine
    {
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
        /// Gets or constructs a shared Tts::Engine instance of type @c T with @p args
        /// @note If an engine of this type exists, a shared instance will be returned instead.
        template<class T, typename... Args>
        static auto make(Args &&... args) -> std::shared_ptr<Engine>
        {
            static_assert(std::is_base_of<Engine, T>::value, "T must be a child of Tts::Engine");
            return IEngine::make<Engine, T>(std::forward<Args>(args)...);
        }

    public:
        /// Gets the TTS channel named @p name with no voice activated yet
        auto channel(std::string const & name) -> std::shared_ptr<Channel>;

        /// Gets the TTS channel named @p name with a specific voice activated
        auto channel(std::string const & name, std::string const & voice)
            -> std::shared_ptr<Channel>;

    public:
        /// Gets available voices per channel for all configured channels
        /// @return @c map<channel_name, @c vector<voice>>
        auto availableVoices() const -> ChannelVoices const &;

    protected:
        virtual auto makeChannel(std::string name) -> Channel * = 0;

        /// @note If @c info.empty() then removes entry
        void setAvailableVoicesForChannel(std::string const & name, std::vector<std::string> info);

    private:
        struct Pimpl;
        Pimpl * _pimpl;
    };

    using EnginePtr = std::shared_ptr<Engine>;
}} // !namespace Vsdk::Tts
