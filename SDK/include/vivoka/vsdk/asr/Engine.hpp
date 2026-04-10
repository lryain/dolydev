/// @file      Engine.hpp
/// @author    Pierre Caissial
/// @date      Created on 27/02/2021
/// @copyright Copyright (c) Vivoka (vivoka.com)

#pragma once

// Project includes
#include "../details/vsdk/IEngine.hpp"

namespace Vsdk { namespace Asr
{
    class DynamicModel;
    class Recognizer;

    /// Responsible for creation of Recognizer and DynamicModel instances
    class Engine : public Vsdk::details::IEngine
    {
    public:
        struct ModelInfo
        {
            enum class Type { Grammar, FreeSpeech, Dictation };

            Type                     type;
            std::string              name, language;
            std::vector<std::string> slots;
        };

        struct RecognizerInfo
        {
            enum class Type : uint8_t
            {
                Unknown,

                Grammar    = 0b001,
                FreeSpeech = 0b010,
                Dictation  = 0b100,

                Any        = Grammar | FreeSpeech | Dictation,
            };

            Type                     type;
            std::string              name;
            std::vector<std::string> languages;

            bool supports(Type t) const;
            bool supports(ModelInfo const & model) const;
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
        ~Engine() noexcept;

    public:
        /// Gets or constructs a shared Asr::Engine instance of type @c T with @p args
        /// @note If an engine of this type exists, a shared instance will be returned instead.
        template<class T, typename... Args>
        static auto make(Args &&... args) -> std::shared_ptr<Engine>
        {
            static_assert(std::is_base_of<Engine, T>::value, "T must be a child of Asr::Engine");
            return IEngine::make<Engine, T>(std::forward<Args>(args)...);
        }

    public:
        /// Gets or constructs a shared Recognizer instance that's been previously configured
        /// @param name Found in the configuration of this engine, name of the Recognizer you want
        auto recognizer(std::string const & name) -> std::shared_ptr<Recognizer>;

        /// Gets or constructs a shared DynamicModel instance that's been previously configured
        /// @param name Found in the configuration of this engine, name of the model you want
        auto dynamicModel(std::string const & name) -> std::shared_ptr<DynamicModel>;

    public:
        auto recognizersInfo() const -> std::unordered_map<std::string, RecognizerInfo> const &;
        auto modelsInfo()      const -> std::unordered_map<std::string, ModelInfo> const &;

    protected:
        virtual auto makeRecognizer  (std::string name) -> Recognizer   * = 0;
        virtual auto makeDynamicModel(std::string name) -> DynamicModel * = 0;

    protected:
        /// @note If @c info.name.empty() then removes entry
        void setRecognizerInfo(std::string const & name, RecognizerInfo info);

        /// @note If @c info.name.empty() then removes entry
        void setModelInfo(std::string const & name, ModelInfo info);

    private:
        struct Pimpl;
        Pimpl * _pimpl;
    };

    using EnginePtr = std::shared_ptr<Engine>;

    auto operator|(Engine::RecognizerInfo::Type lhs, Engine::RecognizerInfo::Type rhs)
        -> Engine::RecognizerInfo::Type;
}} // !namespace Vsdk::Asr
