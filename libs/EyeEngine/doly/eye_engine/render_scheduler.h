#pragma once

#include <atomic>
#include <memory>
#include <thread>

#include "doly/eye_engine/animation_player_v2.h"
#include "doly/eye_engine/engine_control.h"

namespace doly::eye_engine {

/**
 * Render scheduler managing dual-threaded eye rendering with command queue.
 * Processes commands asynchronously and maintains left/right eye synchronization.
 */
class RenderScheduler {
public:
    RenderScheduler(std::unique_ptr<AnimationPlayerV2> player, CommandQueue& command_queue);

    ~RenderScheduler();

    // Start the scheduler thread
    bool start();

    // Stop the scheduler thread
    void stop();

    // Check if scheduler is running
    bool isRunning() const { return running_.load(); }

private:
    void schedulerLoop();

    std::unique_ptr<AnimationPlayerV2> player_;
    CommandQueue& command_queue_;

    std::atomic<bool> running_{false};
    std::thread scheduler_thread_;
};

}  // namespace doly::eye_engine