/// @file      EventLoop.hpp
/// @date      Created on 23/2/2023
/// @copyright Copyright (c) Vivoka (vivoka.com)

#pragma once

// Project includes
#include <vsdk-samples-utils_export.hpp>

// C++ includes
#include <functional>

namespace Vsdk::Utils::Samples
{
    class VSDK_SAMPLES_UTILS_EXPORT EventLoop
    {
    private:
        EventLoop();
        ~EventLoop();
        EventLoop(EventLoop const &)      = delete;
        EventLoop(EventLoop &&)           = delete;
        void operator=(EventLoop const &) = delete;
        void operator=(EventLoop &&)      = delete;

    public:
        static auto instance() -> EventLoop &;
        static void destroy();

    public:
        void run();
        void restart();
        void shutdown();

        template<typename F, typename... Args>
        void queue(F && f, Args &&... args);

    private:
        void queue(std::function<void()> f);

    private:
        struct Pimpl;
        Pimpl *            _pimpl;
        static EventLoop * _instance;
    };

    template<typename F, typename... Args>
    inline void EventLoop::queue(F && f, Args &&... args)
    {
        std::function<void()> fn = std::bind(std::forward<F>(f), std::forward<Args>(args)...);
        queue(std::move(fn));
    }
} // !namespace Vsdk::Utils::Samples
