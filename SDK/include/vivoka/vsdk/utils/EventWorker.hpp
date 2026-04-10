/// @file      EventWorker.hpp
/// @author    BOURAOUI Al-Moez L.A
/// @date      Created on 01/06/2023
/// @copyright Copyright (c) Vivoka (vivoka.com)

#pragma once

// C++ includes
#include <atomic>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <queue>
#include <thread>

class EventWorker
{
private:
    std::atomic_bool                  _running;
    std::thread                       _thread;
    std::mutex                        _taskMutex;
    std::condition_variable           _taskCondition;
    std::queue<std::function<void()>> _tasks;

public:
    EventWorker();

public:
    void start();
    void stop();

    template<typename Func, typename... Args>
    void pushTask(Func && func, Args &&... args)
    {
        {
            std::lock_guard<std::mutex> lock(_taskMutex);
            _tasks.emplace(std::bind(std::forward<Func>(func), std::forward<Args>(args)...));
        }
        _taskCondition.notify_one();
    }

private:
    void run();
};
