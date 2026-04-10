#pragma once

#include "doly/eye_engine/widgets/widget_interface.h"
#include "doly/eye_engine/lcd_transport.h"
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace doly {
namespace eye_engine {

/**
 * @brief Manager for all UI widgets
 * 
 * Handles registration, update, and rendering of widgets.
 * Collects ROIs for efficient partial screen updates.
 */
class WidgetManager {
public:
    WidgetManager() = default;
    ~WidgetManager() = default;
    
    // Widget lifecycle
    void registerWidget(const std::string& name, std::unique_ptr<IWidget> widget);
    void removeWidget(const std::string& name);
    IWidget* getWidget(const std::string& name);
    bool hasWidget(const std::string& name) const;
    
    // Update all widgets
    void updateAll(double delta_time_ms);
    
    // Render all enabled widgets (force redraw when widget is dominant)
    void renderAll(FrameBuffer& buffer, LcdSide side, bool force_redraw = false);
    
    // Collect ROIs from all widgets that need updates
    std::vector<ROI> collectUpdateROIs() const;
    
    // Get merged ROI for all widget updates
    ROI getMergedUpdateROI() const;
    
    // Update widget configuration
    bool updateWidgetConfig(const std::string& name, const std::string& config_json);
    
    // Enable/disable widget
    bool setWidgetEnabled(const std::string& name, bool enabled);

    // Enable/disable widget core logic (background mode)
    bool setWidgetCoreEnabled(const std::string& name, bool enabled);
    
    // Get all widget names
    std::vector<std::string> getWidgetNames() const;
    
    // Clear all widgets
    void clear();
    
    // Get total rendering budget used (milliseconds)
    double getTotalRenderTime() const { return total_render_time_ms_; }
    
    // Set maximum render time budget (milliseconds)
    void setMaxRenderTime(double max_ms) { max_render_time_ms_ = max_ms; }
    
private:
    std::map<std::string, std::unique_ptr<IWidget>> widgets_;
    std::map<std::string, std::string> widget_configs_;
    double total_render_time_ms_ = 0.0;
    double max_render_time_ms_ = 3.0;  // Default 3ms budget
    std::uint64_t frame_counter_ = 0;
    double last_delta_time_ms_ = 0.0;
};

}  // namespace eye_engine
}  // namespace doly
