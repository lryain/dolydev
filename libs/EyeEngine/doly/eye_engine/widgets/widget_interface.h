#pragma once

#include <cstdint>
#include <memory>
#include <string>

#include <nlohmann/json.hpp>

#include "doly/eye_engine/lcd_transport.h"

namespace doly {
namespace eye_engine {

// Forward declarations
class FrameBuffer;

/**
 * @brief Region of Interest for partial screen updates
 */
struct ROI {
    int x;      // Top-left x coordinate
    int y;      // Top-left y coordinate
    int width;  // Width in pixels
    int height; // Height in pixels
    
    bool isEmpty() const { return width <= 0 || height <= 0; }
    
    // Merge two ROIs to get minimum bounding rectangle
    static ROI merge(const ROI& a, const ROI& b);
};

struct WidgetContext {
    LcdSide side = LcdSide::LcdLeft;
    int width = 240;
    int height = 240;
    bool force_redraw = false;
    bool is_active = false;
    std::uint64_t frame_index = 0;
    double delta_time_ms = 0.0;
};

/**
 * @brief Anchor point for widget positioning
 */
enum class Anchor {
    TopLeft,
    TopCenter,
    TopRight,
    CenterLeft,
    Center,
    CenterRight,
    BottomLeft,
    BottomCenter,
    BottomRight
};

/**
 * @brief Widget position configuration
 */
struct WidgetPosition {
    Anchor anchor = Anchor::TopLeft;
    int offset_x = 0;  // Pixels from anchor point
    int offset_y = 0;
    
    // Calculate absolute position on screen
    void calculateAbsolute(int screen_width, int screen_height,
                          int widget_width, int widget_height,
                          int& out_x, int& out_y) const;
};

/**
 * @brief Abstract interface for UI widgets
 * 
 * Widgets are independent visual components that can be overlaid on top of
 * the eye rendering. Each widget manages its own rendering buffer, update
 * cycle, and ROI for efficient partial updates.
 */
class IWidget {
public:
    virtual ~IWidget() = default;

    /**
     * @brief Called when widget becomes visible
     */
    virtual void onShow(const nlohmann::json& config) = 0;

    /**
     * @brief Called when widget is hidden
     */
    virtual void onHide() = 0;
    
    /**
     * @brief Update widget state (called every frame)
     * @param delta_time_ms Time elapsed since last update in milliseconds
     */
    virtual void update(double delta_time_ms) = 0;
    
    /**
    * @brief Prepare widget for rendering this frame
    * @param ctx Rendering context metadata
    */
    virtual void prepareFrame(const WidgetContext& ctx) = 0;

    /**
    * @brief Render widget to frame buffer for a specific LCD side
    * @param buffer Target frame buffer (240x240 RGB565)
    * @param ctx    Rendering context metadata
     */
    virtual void render(FrameBuffer& buffer, const WidgetContext& ctx) = 0;
    
    /**
     * @brief Get region that needs to be updated this frame
     * @return ROI describing the update region, or empty ROI if no update needed
     */
    virtual ROI getUpdateROI() const = 0;
    
    /**
     * @brief Check if widget needs redrawing this frame
     * @return true if render() should be called
     */
    virtual bool needsRedraw() const = 0;
    
    /**
     * @brief Enable or disable the widget
     * @param enabled true to show widget, false to hide
     */
    virtual void setEnabled(bool enabled) = 0;
    
    /**
     * @brief Check if widget is currently enabled
     */
    virtual bool isEnabled() const = 0;

    /**
     * @brief Enable/disable core logic while allowing rendering to stay hidden.
     * @param enabled true to keep the widget core running in background
     */
    virtual void setCoreEnabled(bool enabled) = 0;

    /**
     * @brief Check if widget core should run even when hidden
     */
    virtual bool isCoreEnabled() const = 0;

    /**
     * @brief Check if widget core is active (visible or background enabled)
     */
    virtual bool isCoreActive() const = 0;
    
    /**
     * @brief Set widget opacity
     * @param opacity Alpha value [0.0, 1.0]
     */
    virtual void setOpacity(float opacity) = 0;
    
    /**
     * @brief Get current opacity
     */
    virtual float getOpacity() const = 0;
    
    /**
     * @brief Update widget configuration from JSON
     * @param config_json JSON configuration string
     * @return true if configuration was applied successfully
     */
    virtual bool updateConfig(const std::string& config_json) = 0;
    
    /**
     * @brief Get current configuration as JSON
     */
    virtual std::string getConfig() const = 0;
    
    /**
     * @brief Get widget type identifier
     */
    virtual const char* getType() const = 0;
    
    /**
     * @brief Reset widget to initial state
     */
    virtual void reset() = 0;
};

/**
 * @brief Base implementation of IWidget with common functionality
 */
class WidgetBase : public IWidget {
public:
    explicit WidgetBase(const char* type);
    ~WidgetBase() override = default;

    void onShow(const nlohmann::json& config) override;
    void onHide() override;
    void prepareFrame(const WidgetContext& ctx) override;

    void setEnabled(bool enabled) override;
    bool isEnabled() const override { return enabled_; }
    void setCoreEnabled(bool enabled) override;
    bool isCoreEnabled() const override { return core_enabled_; }
    bool isCoreActive() const override { return enabled_ || core_enabled_; }
    void setOpacity(float opacity) override;
    float getOpacity() const override { return opacity_; }
    const char* getType() const override { return type_; }

    const WidgetContext& lastContext() const { return last_context_; }
    const nlohmann::json& lastConfig() const { return last_config_; }

    // Position management
    void setPosition(const WidgetPosition& position) { position_ = position; }
    const WidgetPosition& getPosition() const { return position_; }

    // Update interval management
    void setUpdateInterval(int interval_ms) { update_interval_ms_ = interval_ms; }
    int getUpdateInterval() const { return update_interval_ms_; }

protected:
    // Check if enough time has elapsed for next update
    bool shouldUpdate(double delta_time_ms);

    // Mark widget as dirty (needs redraw)
    void markDirty() { needs_redraw_ = true; }

    // Clear dirty flag after rendering
    void clearDirty() { needs_redraw_ = false; }

    void cacheConfig(const nlohmann::json& cfg) { last_config_ = cfg; }

    const char* type_;
    bool enabled_ = false;
    bool core_enabled_ = false;
    float opacity_ = 1.0f;
    bool needs_redraw_ = true;

    WidgetPosition position_;
    int update_interval_ms_ = 100;  // Default 100ms update interval
    double time_since_update_ms_ = 0.0;
    WidgetContext last_context_{};
    nlohmann::json last_config_ = nlohmann::json::object();
};

}  // namespace eye_engine
}  // namespace doly
