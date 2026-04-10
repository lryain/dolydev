/// @file      IEngine.hpp
/// @author    Pierre Caissial
/// @date      Created on 27/02/2021
/// @copyright Copyright (c) Vivoka (vivoka.com)

#pragma once

// Third-party includes
#include <nlohmann/json.hpp>

// C++ includes
#include <memory>
#include <string>

namespace Vsdk { namespace details
{
    /// Base class of all engines
    class IEngine : public std::enable_shared_from_this<IEngine>
    {
    protected:
        /// @param name       Name of the configuration object in @c vsdk.json
        /// @param configPath Path to a @c vsdk.json formatted file
        IEngine(char const * name, std::string const & configPath);

        /// @param name   Name of the configuration object in @c vsdk.json
        /// @param config A @c vsdk.json formatted JSON configuration object
        IEngine(char const * name, nlohmann::json config);

        /// Specialization used to avoid overload collisions,
        /// forwards to <tt>IEngine(char const*, std::string const &);</tt>
        /// @param name       Name of the configuration object in @c vsdk.json
        /// @param configPath Path to a @c vsdk.json formatted file
        IEngine(char const * name, char const * configPath);

    public:
        IEngine(IEngine const &) noexcept = delete;
        IEngine & operator=(IEngine const &) noexcept = delete;
        IEngine(IEngine &&) noexcept = delete;
        IEngine & operator=(IEngine &&) noexcept = delete;
        virtual ~IEngine() noexcept = 0;

    public:
        /// Formats a version string of the child engine's version
        /// @note Not the same as @c Vsdk::version()
        virtual auto version() const -> std::string = 0;

        auto config() const -> nlohmann::json const &;

        /// Accesses the child engine. Should be avoided unless native features are needed
        /// @tparam T   Child engine class given when previously invoking make()
        /// @return     A shared pointer from @c this
        template<class T> auto asNativeEngine() -> std::shared_ptr<T>
        {
            static_assert(std::is_base_of<IEngine, T>::value, "T is not a child of IEngine");
            return std::static_pointer_cast<T>(shared_from_this());
        }

    public:
        static auto loadConfigFile(std::string const & configPath) -> nlohmann::json;

    protected:
        auto config() -> nlohmann::json &;

        /// Performs extra initialization steps <b>before</b> @c checkConfig() is called
        virtual void init();

        /// Performs configuration validation
        /// @note    The configuration does <b>not</b> contains the whole vsdk.json file,
        ///          only the object under the engine's <tt>name</tt> constructor parameter key name
        /// @throws Vsdk::Exception if the configuration is invalid
        virtual void checkConfig() const;

        /// Creates an engine using the specified provider child engine class
        /// @tparam T Child class of Engine
        /// @return   A polymorphic shared Engine pointer containing an instance of T
        /// @warning  Calling this function a second time while a previous engine of type T
        ///           is still alive will return a pointer to the existing engine
        template<class U, class T, typename... Args>
        static auto make(Args &&... args) -> std::shared_ptr<U>
        {
            static_assert(std::is_base_of<IEngine, U>::value, "U must inherit IEngine class");
            static_assert(std::is_base_of<U, T>::value,       "T must inherit U");

            auto engine = makeImpl<U, T>(nullptr);
            if (!engine)
            {
                engine = makeImpl<U, T>(std::unique_ptr<T>{new T(std::forward<Args>(args)...)});
                initEngine(*engine);
            }
            return engine;
        }

    private:
        /// This function exists in order to remove the weak_ptr's dependency to the variadic args
        template<class U, class T>
        static auto makeImpl(std::unique_ptr<T> engineRawPtr) -> std::shared_ptr<U>
        {
            static std::weak_ptr<T> engine;

            auto ptr = engine.lock();
            if (!ptr && engineRawPtr)
            {
                ptr.reset(engineRawPtr.release());
                engine = ptr;
            }
            return ptr;
        }

        static void initEngine(IEngine & engine);

    private:
        struct Pimpl;
        std::unique_ptr<Pimpl> _pimpl;
    };
}} // !namespace Vsdk
