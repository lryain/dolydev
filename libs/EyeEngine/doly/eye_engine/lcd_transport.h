#pragma once

#include <cstdint>
#include <cstddef>
#include <functional>
#include <string>
#include <vector>

#include "LcdControl.h"

namespace doly::eye_engine {

/**
 * \brief Runtime configuration for a single circular LCD panel.
 */
struct DisplayConfig {
    LcdSide side;                ///< Physical side (left/right).
    bool mirror_x{false};        ///< Mirror horizontally (useful for mechanical flips).
    bool mirror_y{false};        ///< Mirror vertically.
    bool swap_rgb{false};        ///< Swap RGB to BGR ordering when sending frames.
    LcdColorDepth depth{LCD_12BIT}; ///< Active color depth. 统一使用 LCD_12BIT
    uint32_t spi_frequency_hz{32000000}; ///< Effective SPI frequency in Hz.
};

/**
 * \brief ROI (Region of Interest) rectangle in pixel coordinates.
 */
struct FrameRegion {
    uint16_t x{0};
    uint16_t y{0};
    uint16_t width{0};
    uint16_t height{0};
};

/**
 * \brief EyeEngine hardware abstraction for LCD transport.
 *
 * Implementations provide initialization, buffer management and frame submission
 * while hiding raw LcdControl details. All methods are expected to be thread-safe.
 */
class LcdTransport {
public:
    virtual ~LcdTransport() = default;

    /**
     * Initialize hardware interface and allocate internal resources.
     * @param depth   Desired color depth (12-bit or 18-bit).
     * @param configs Per-side configuration descriptors.
     * @return true if initialization succeeds, false otherwise.
     */
    virtual bool init(LcdColorDepth depth, const std::vector<DisplayConfig>& configs) = 0;

    /**
     * Release device resources (idempotent).
     */
    virtual void shutdown() = 0;

    /**
     * Query the buffer size in bytes required for a full-frame submission.
     */
    virtual std::size_t framebufferSize() const = 0;

    /**
     * Submit a full-frame buffer for the given side.
     * @param side   Target LCD.
     * @param buffer Pointer to packed pixel data (size = framebufferSize()).
     * @return true on success.
     */
    virtual bool submitFrame(LcdSide side, const std::uint8_t* buffer) = 0;

    /**
     * Submit a partial frame update (ROI). Implementations may fall back to
     * full-frame writes when ROI is unsupported.
     * @param side   Target LCD.
     * @param region Rectangle describing the update area.
     * @param buffer Packed pixel data for the specified region.
     * @param stride Bytes per row in buffer (defaults to region.width * bytesPerPixel()).
     * @return true on success.
     */
    virtual bool submitRegion(LcdSide side,
                              const FrameRegion& region,
                              const std::uint8_t* buffer,
                              std::size_t stride) = 0;

    /**
     * Adjust backlight brightness (normalized 0.0 – 1.0).
     */
    virtual bool setBacklight(float normalized) = 0;

    /**
     * Fetch runtime configuration for diagnostics.
     */
    virtual const std::vector<DisplayConfig>& configs() const = 0;

    /**
     * Optional hook executed after each frame submission. Used for diagnostics
     * (e.g., collecting per-frame timings).
     */
    using SubmissionHook = std::function<void(LcdSide side, std::uint64_t timestamp_ns)>;

    virtual void setSubmissionHook(SubmissionHook hook) = 0;
};

/**
 * Factory helper returning the default implementation backed by LcdControl.
 * The returned instance must be deleted by the caller.
 */
LcdTransport* createLcdControlTransport();

} // namespace doly::eye_engine
