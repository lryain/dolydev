/// @file      Engine.hpp
/// @author    Vincent Leroy
/// @date      Created on 25/1/2023
/// @copyright Copyright (c) 2023 Vivoka (vivoka.com)

#pragma once

// Project includes
#include "../details/vsdk/IEngine.hpp"

namespace Vsdk { namespace Nlu
{
    class Parser;

    /// Responsible for creation of Parser instances
    class Engine : public Vsdk::details::IEngine
    {
    public:
        struct ParserInfo
        {
            std::string name, language;
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
        /// Gets or constructs a shared Nlu::Engine instance of type @c T with @p args
        /// @note If an engine of this type exists, a shared instance will be returned instead.
        template<class T, typename... Args>
        static auto make(Args &&... args) -> std::shared_ptr<Engine>
        {
            static_assert(std::is_base_of<Engine, T>::value, "T must be a child of Nlu::Engine");
            return IEngine::make<Engine, T>(std::forward<Args>(args)...);
        }

    public:
        /// Gets or constructs a shared Parser instance that's been previously configured
        /// @param name Found in the configuration of this engine, name of the Parser you want
        auto parser(std::string const & name) -> std::shared_ptr<Parser>;

        /// Gets available parsers info
        auto parsersInfo() const -> std::unordered_map<std::string, ParserInfo> const &;

    protected:
        virtual auto makeParser(std::string name) -> Parser * = 0;

        /// @note If @c info.name.empty() then removes entry
        void setParserInfo(std::string const & name, ParserInfo info);

    private:
        struct Pimpl;
        Pimpl * _pimpl;
    };

    using EnginePtr = std::shared_ptr<Engine>;
}} // !namespace Vsdk::Nlu
