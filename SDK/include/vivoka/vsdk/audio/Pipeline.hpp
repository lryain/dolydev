/// @file      Pipeline.hpp
/// @author    Pierre Caissial
/// @date      Created on 17/05/2021
/// @copyright Copyright (c) Vivoka (vivoka.com)

#pragma once

// Project includes
#include "Buffer.hpp"

#include <vsdk/Exception.hpp>

// Third-paty includes
#include <fmt/core.h>

// C++ includes
#include <atomic>
#include <functional>
#include <list>
#include <memory>
#include <optional>

namespace Vsdk { namespace Audio
{
    namespace details
    {
        template<class C>
        class Iterator
        {
        private:
            using IteratorType = typename C::const_iterator;
            IteratorType _it;

        public:
            Iterator(IteratorType it) noexcept: _it(it) {} // NOLINT

        public:
            operator IteratorType() const { return _it; } // NOLINT
            auto operator*()        -> typename C::value_type   { return *_it;       }
            auto operator*()  const -> typename C::value_type   { return *_it;       }
            auto operator->()       -> typename C::value_type * { return _it->get(); }
            auto operator->() const -> typename C::value_type * { return _it->get(); }

            /// Gets a dynamically casted shared pointer to the underlying pointed object
            template<class T>
            auto get() const -> std::shared_ptr<T> { return std::dynamic_pointer_cast<T>(*_it); }
        };
    } // !namespace details

    /// Base class for Pipeline modules that aquire and dispatch audio buffers (like Producer::File)
    class ProducerModule
    {
        friend class Pipeline;

    public:
        enum class State
        {
            Closed,  ///< Ready to be <code>open()</code>-ed
            Opened,  ///< Ready to call @c run() or @c start()
            Running, ///< Inside of @c run()
            Started, ///< @c start() (or @c resume()) has been called and work is in progress
            Paused,  ///< @c pause() has been called
            Idle,    ///< Either @c stop() has been called or audio production is over,
                     ///< new work can be launched
        };

    private:
        using AudioCallback = std::function<void(Buffer, bool)>;
        std::list<AudioCallback> _subscribers;
        Vsdk::ExceptionStack     _lastExceptionStack;

    protected:
        std::atomic<State> _state;

    public:
        /// Iterator pointing to a subscriber of this producer
        using Iterator = details::Iterator<decltype(_subscribers)>;

    public:
        ProducerModule();
        ProducerModule(ProducerModule &&);
        ProducerModule & operator=(ProducerModule &&);
        virtual ~ProducerModule() = 0;

        ProducerModule(ProducerModule const &) = delete;
        ProducerModule & operator=(ProducerModule const &) = delete;

    public:
        /// Subscribes as first in the list of audio buffer receivers for this producer
        auto subscribeFirst(AudioCallback c) -> Iterator;
        /// Subscribes as last in the list of audio buffer receivers for this producer
        auto subscribeLast (AudioCallback c) -> Iterator;
        /// Unsubscribes from this producer audio buffer dispatch list
        void unsubscribe(Iterator it) noexcept;

    public:
        auto lastError() const -> std::string;
        auto state()     const -> State;
        bool isOpened()  const;
        bool isRunning() const;
        bool isStarted() const;
        bool isPaused()  const;
        bool isIdle()    const;
        bool isClosed()  const;

    public:
        /// Attempts to open the underlying resource(s) of the producer
        /// @note <ul>
        ///         <li>Blocking call</li>
        ///         <li>@c lastError() is reset at the beginning of this function</li>
        ///       </ul>
        /// @post  <code>state() == State::Opened</code>
        bool open() noexcept;

        /// @note   <ul>
        ///           <li>Blocking call, no asynchronous work should be performed</li>
        ///           <li>@c lastError() is reset at the beginning of this function</li>
        ///         </ul>
        /// @warning Do not override if you do not support synchronous operations!
        /// @pre     <code>state() == State::Opened</code>
        /// @post    <code>state() == State::Opened</code>
        bool run() noexcept;

        /// @note    <ul>
        ///            <li>Non-blocking call: producer might not be started right after this</li>
        ///            <li>@c lastError() is reset at the beginning of this function</li>
        ///          </ul>
        /// @warning Do not override if you do not support async operations!
        /// @pre     <code>state() == State::Opened</code>
        bool start() noexcept;

        /// Attempts to pause a started producer
        /// @note    <ul>
        ///            <li>Non-blocking call: producer might not be paused right after this</li>
        ///            <li>@c lastError() is reset at the beginning of this function</li>
        ///          </ul>
        /// @warning Do not override if you do not support pausing!
        bool pause() noexcept;

        /// Attempts to resume a paused producer
        /// @note    <ul>
        ///            <li>Non-blocking call: producer might not be resumed right after this</li>
        ///            <li>@c lastError() is reset at the beginning of this function</li>
        ///          </ul>
        /// @warning Do not override if you do not support resuming!
        bool resume() noexcept;

        /// Attempts to stop a started producer
        /// @note   <ul>
        ///            <li>Non-blocking call: producer state might not be @c Idle after this</li>
        ///            <li>@c lastError() is reset at the beginning of this function</li>
        ///          </ul>
        /// @warning Do not override if you do not support async operations!
        bool stop() noexcept;

        /// @note    <ul>
        ///           <li>Blocking call: producer should wait for proper closing</li>
        ///           <li>@c lastError() is reset at the beginning of this function</li>
        ///          </ul>
        /// @warning <ul>
        ///            <li>Attempting to close a running producer will fail</li>
        ///            <li>Attempting to close a started producer will first try to close it</li>
        ///          </ul>
        /// @post    <code>state() == State::Closed</code>
        bool close() noexcept;

    protected:
        virtual void openImpl();
        virtual void runImpl();
        virtual void startImpl();
        virtual void pauseImpl();
        virtual void resumeImpl();
        virtual void stopImpl();
        virtual void closeImpl();

    protected:
        void dispatchBuffer(Buffer buffer, bool last);

        template<typename T>
        void dispatchAudio(T && data, int sampleRate, int channelCount, bool isLast) {
            dispatchBuffer({ std::forward<T>(data), sampleRate, channelCount }, isLast);
        }
    };
    using ProducerPtr = std::shared_ptr<ProducerModule>;

    /// Producer modules with an accessible constructor should inherit this one
    template<class T>
    struct ProducerModuleImpl : public ProducerModule
    {
        ProducerModuleImpl() = default;

        template<typename... Args>
        static inline auto make(Args &&... args) -> std::shared_ptr<T> {
            return std::shared_ptr<T>(new T(std::forward<Args>(args)...));
        }
    };

    /// Base class for Pipeline modules that modify audio buffers (like Afe::Filter)
    struct ModifierModule
    {
        virtual ~ModifierModule() = default;
        virtual void process(Buffer &, bool last) = 0;
    };
    using ModifierPtr = std::shared_ptr<ModifierModule>;

    /// Base class for Pipeline modules that consume audio buffers (like Consumer::File)
    struct ConsumerModule
    {
        virtual ~ConsumerModule() = default;
        virtual void process(Buffer const &, bool last) = 0;
    };
    using ConsumerPtr = std::shared_ptr<ConsumerModule>;

    /// Route audio from a ProducerModule to ConsumerModule, with ModifierModule in the middle
    class Pipeline
    {
    private:
        std::shared_ptr<ProducerModule>         _producer;
        std::optional<ProducerModule::Iterator> _position;
        std::list<ModifierPtr>                  _modifiers;
        std::list<ConsumerPtr>                  _consumers;
        Vsdk::ExceptionStack                    _lastExceptionStack;

    public:
        using ModifierIterator = details::Iterator<decltype(_modifiers)>;
        using ConsumerIterator = details::Iterator<decltype(_consumers)>;

    public:
        Pipeline() = default;
        /// Cannot copy because Modifiers & Consumers cannot be shared nor cloned
        Pipeline(Pipeline const & other) noexcept = delete;
        /// Cannot copy because Modifiers & Consumers cannot be shared nor cloned
        Pipeline & operator=(Pipeline const & other) noexcept = delete;
        Pipeline(Pipeline && other);
        Pipeline & operator=(Pipeline &&);
        /// Stops the pipeline if running and unsubscribe from the producer
        ~Pipeline();

    public:
        /// Synchronously runs the pipeline (if supported by the provider). Effectively calls:
        /// @code
        ///     producer.open();
        ///     producer.run();
        ///     producer.close();
        /// @endcode
        bool run() noexcept;

        /// Asynchronously starts the pipeline (if supported by the provider). Effectively calls:
        /// @code
        ///     producer.open();
        ///     producer.start();
        /// @endcode
        bool start() noexcept;

        /// Attempts to pause an already started pipeline
        /// @note This will send an empty "last" buffer into the pipeline if pausing succeeds
        bool pause() noexcept;

        /// Attempts to resume a paused pipeline
        bool resume() noexcept;

        /// Stops a pipeline that's been start()-ed. Effectively calls:
        /// @code
        ///     producer.stop();
        ///     producer.close();
        /// @endcode
        bool stop() noexcept;

    public:
        /// Sets a new producer that's already been constructed outside
        /// @note If the pipeline is running it will first be stopped before setting the producer.
        void setProducer(ProducerPtr prod);

        /// Sets a new producer by constructing it with @p args
        template<class T, typename... Args>
        auto setProducer(Args &&... args) -> std::shared_ptr<T>
        {
            setProducer(ProducerModuleImpl<T>::make(std::forward<Args>(args)...));
            return std::static_pointer_cast<T>(producer());
        }

        template<class T, typename... Args>
        auto pushBackModifier(Args &&... args) -> ModifierIterator {
            return insertModifier<T>(_modifiers.cend(), std::forward<Args>(args)...);
        }
        template<class T, typename... Args>
        auto pushFrontModifier(Args &&... args) -> ModifierIterator {
            return insertModifier<T>(_modifiers.cbegin(), std::forward<Args>(args)...);
        }
        template<class T, typename... Args>
        auto pushBackConsumer(Args &&... args) -> ConsumerIterator {
            return insertConsumer<T>(_consumers.cend(), std::forward<Args>(args)...);
        }
        template<class T, typename... Args>
        auto pushFrontConsumer(Args &&... args) -> ConsumerIterator {
            return insertConsumer<T>(_consumers.cbegin(), std::forward<Args>(args)...);
        }
        template<class T, typename... Args>
        auto insertModifier(ModifierIterator pos, Args &&... args) -> ModifierIterator
        {
            static_assert(std::is_base_of<ModifierModule, T>::value,
                          "T must inherit ModifierModule");
            return insertModifier(pos, std::make_shared<T>(std::forward<Args>(args)...));
        }
        template<class T, typename... Args>
        auto insertConsumer(ConsumerIterator pos, Args &&... args) -> ConsumerIterator
        {
            static_assert(std::is_base_of<ConsumerModule, T>::value,
                          "T must inherit ConsumerModule");
            return insertConsumer(pos, std::make_shared<T>(std::forward<Args>(args)...));
        }
        auto pushBackModifier(ModifierPtr ptr)  -> ModifierIterator;
        auto pushBackConsumer(ConsumerPtr ptr)  -> ConsumerIterator;
        auto pushFrontModifier(ModifierPtr ptr) -> ModifierIterator;
        auto pushFrontConsumer(ConsumerPtr ptr) -> ConsumerIterator;
        auto insertModifier(ModifierIterator pos, ModifierPtr ptr) -> ModifierIterator;
        auto insertConsumer(ConsumerIterator pos, ConsumerPtr ptr) -> ConsumerIterator;
        void eraseModifier(ModifierIterator it);
        void eraseConsumer(ConsumerIterator it);
        void clearModifiers();
        void clearConsumers();

    public:
        /// Produces a string formatted from the last generated ExceptionStack. This is akin to
        /// calling @c Vsdk::printExceptionStack() with default parameters
        auto lastError() const -> std::string;

        /// Gets this pipeline's producer in a generic form.
        auto producer() const -> ProducerPtr;

        /// Gets this pipeline's producer statically casted to @p T
        /// @note Use the non-templated overload if you need to perform a type check
        template<typename T>
        auto producer() const -> std::shared_ptr<T>
        {
            static_assert(std::is_base_of<ProducerModule, T>::value,
                          "T must inherit ProducerModule");
            return std::static_pointer_cast<T>(_producer);
        }

    private:
        void unsubscribeFromProducer() noexcept;
        void onAudioBufferReceived(Buffer buffer, bool last);
    };
}} // !namespace Vsdk::Audio
