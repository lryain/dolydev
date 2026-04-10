/// @file      Engine.hpp
/// @author    Pierre Caissial
/// @date      Created on 25/05/2021
/// @copyright Copyright (c) Vivoka (vivoka.com)

#pragma once

// Project includes
#include "../details/vsdk/IEngine.hpp"
#include "ModelType.hpp"

namespace Vsdk { namespace Biometrics
{
    class Authenticator;
    class Identificator;
    class Model;

    /// Responsible for creation of Authenticator and Identificator instances
    class Engine : public Vsdk::details::IEngine
    {
        friend class Model;

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
        /// Gets or constructs a shared Biometrics::Engine instance of type @c T with @p args
        /// @note If an engine of this type exists, a shared instance will be returned instead.
        template<class T, typename... Args>
        static auto make(Args &&... args) -> std::shared_ptr<Engine>
        {
            static_assert(std::is_base_of<Engine, T>::value, "T must inherit Biometrics::Engine");
            return IEngine::make<Engine, T>(std::forward<Args>(args)...);
        }

    public:
        /// Loads model @p name
        auto model(std::string const & name) -> std::shared_ptr<Model>;

        /// Gets the list of already loaded models
        auto loadedModels() const
            -> std::unordered_map<std::string, std::shared_ptr<Model>> const &;

        /// Gets the list of model names that can be loaded
        /// @note Does <b>not</b> contain the already loaded model names
        virtual auto loadableModelNames() const -> std::vector<std::string> = 0;

    public:
        /// Creates a new, empty model of type @p type named @p name
        auto makeModel(std::string name, ModelType type) -> std::shared_ptr<Model>;

        /// Releases the model from the engine
        /// @note    The model will truly unload once the user releases all shared instances of it
        /// @throws  Vsdk::Exception if the model cannot be found
        auto unloadModel(std::string const & name) -> size_t;

        /// Releases the model from the engine
        /// @note    The model will truly unload once the user releases all shared instances of it
        /// @throws  Vsdk::Exception if the model cannot be found
        auto unloadModel(std::shared_ptr<Model> && model) -> size_t;

        /// Releases loaded models from the engine
        /// @note    The models will truly unload once the user releases all shared instances
        ///          for each of them
        void unloadModels();

        /// Unloads (if needed) and deletes the model from the filesystem
        /// @note    The model will truly unload and get deleted once the user releases all shared
        ///          instances of it
        void deleteModel(std::string const & name);

        /// Creates a default constructed authenticator, initialized with @p model and @p threshold
        /// @param threshold Must be between [ 0 ; 10 ]
        auto makeAuthenticator(std::string name, std::shared_ptr<Model> model = nullptr,
                               int threshold = 5) -> std::shared_ptr<Authenticator>;

        /// Creates a default constructed identificator, initialized with @p model and @p threshold
        /// @param threshold Must be between [ 0 ; 10 ]
        auto makeIdentificator(std::string name, std::shared_ptr<Model> model = nullptr,
                               int threshold = 5) -> std::shared_ptr<Identificator>;

    protected:
        virtual bool modelExistsOnFilesystem(std::string const & name) const = 0;
        virtual auto makeModelImpl(std::string name, ModelType type) -> Model * = 0;
        virtual auto makeAuthenticatorImpl(std::string name) -> Authenticator * = 0;
        virtual auto makeIdentificatorImpl(std::string name) -> Identificator * = 0;
        virtual void deleteModelImpl(std::string const & name) = 0;

    private:
        struct Pimpl;
        Pimpl * _pimpl = nullptr;
    };

    using EnginePtr = std::shared_ptr<Engine>;
}} // !namespace Vsdk::Biometrics
