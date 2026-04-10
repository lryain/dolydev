#pragma once

#include <atomic>
#include <memory>
#include <thread>
#include <vector>

#include "doly/eye_engine/eye_renderer.h"
#include "doly/eye_engine/frame_swap_chain.h"
#include "doly/eye_engine/lcd_transport.h"

namespace doly::eye_engine {

enum class RenderMode {
    kProcedural,  // Use EyeRenderer for procedural generation
    kFrameBased   // Use pre-loaded frames (e.g., from StylePack)
};

struct FrameSequence {
    std::vector<std::unique_ptr<std::uint8_t[]>> frames;  // RGB565 buffers
    std::vector<int> delays_ms;  // Delay per frame
};

/**
 * Advanced animation player supporting procedural and frame-based rendering.
 * Supports mode switching and synchronized left/right eye rendering.
 */
class AnimationPlayerV2 {
public:
    AnimationPlayerV2(EyeRenderer& eye_renderer,
                      FrameSwapChain& left_swap_chain,
                      FrameSwapChain& right_swap_chain,
                      LcdTransport& transport);

    ~AnimationPlayerV2();

    // Start the rendering loop in a background thread
    bool start();

    // Stop the rendering loop
    void stop();

    // Switch render mode
    void setMode(RenderMode mode);

    // Load frame sequence for frame-based mode (from StylePack)
    bool loadFrameSequence(FrameSequence&& sequence);

    // Get current mode
    RenderMode mode() const { return mode_.load(); }

    // Check if player is running
    bool isRunning() const { return running_.load(); }

private:
    void renderLoop();
    void renderProcedural();
    void renderFrameBased();

    EyeRenderer& eye_renderer_;
    FrameSwapChain& left_swap_chain_;
    FrameSwapChain& right_swap_chain_;
    LcdTransport& transport_;

    std::atomic<RenderMode> mode_{RenderMode::kProcedural};
    FrameSequence frame_sequence_;

    std::atomic<bool> running_{false};
    std::thread render_thread_;

    // For frame-based animation
    size_t current_frame_{0};
    std::chrono::steady_clock::time_point last_frame_time_;
};

}  // namespace doly::eye_engine