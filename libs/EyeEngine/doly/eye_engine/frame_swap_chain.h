#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>

#include "doly/eye_engine/lcd_transport.h"

namespace doly::eye_engine {

class FrameBuffer {
public:
    FrameBuffer() = default;
    explicit FrameBuffer(std::size_t size) { allocate(size); }

    bool allocate(std::size_t size);
    std::uint8_t* data() { return data_.get(); }
    const std::uint8_t* data() const { return data_.get(); }
    std::size_t size() const { return size_; }
    void clear(std::uint8_t value = 0x00);

private:
    std::unique_ptr<std::uint8_t[]> data_;
    std::size_t size_{0};
};

/**
 * Simple double-buffer swap chain per LCD side.
 * acquireForRender() → render into returned buffer → present().
 */
class FrameSwapChain {
public:
    FrameSwapChain() = default;

    bool initialize(std::size_t buffer_size);

    /**
     * Returns pointer to back buffer for rendering. The caller must ensure
     * `present` is invoked afterwards to swap buffers.
     */
    std::uint8_t* acquireForRender();

    /**
     * Submit back buffer and swap to front.
     */
    bool present(LcdTransport& transport, LcdSide side);

    const FrameBuffer& frontBuffer() const;

private:
    FrameBuffer buffers_[2];
    int front_{0};
    mutable std::mutex mutex_;
};

}  // namespace doly::eye_engine
