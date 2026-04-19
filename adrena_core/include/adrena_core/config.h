#pragma once
#include <windows.h>
#include <string>
#include <cstdint>

namespace adrena {

enum class SGSRMode     { Off, SGSR1, SGSR2 };
enum class Quality      { UltraQuality, Quality, Balanced, Performance, UltraPerformance };
enum class FGMode       { X1, X2, X3, X4 };

struct Config {
    bool      enabled        = false;
    SGSRMode  sgsr_mode      = SGSRMode::SGSR1;

    // If non-empty, overrides sgsr_mode and selects the named upscaler
    // plugin (see PluginManager). "sgsr1" and "sgsr2" map to the built-in
    // passes; external plugin names come from DLLs in the `plugins/` dir.
    std::string upscaler_plugin;

    Quality   quality        = Quality::Quality;
    float     custom_scale   = 0.0f;
    float     sharpness      = 0.80f;

    FGMode    fg_mode        = FGMode::X1;
    int       fps_threshold  = 0;

    bool      overlay_enabled = true;
    int       toggle_key      = VK_HOME;
    bool      fps_display     = true;
    float     overlay_opacity = 0.85f;

    bool      force_d3d11    = false;
    int       rt_format      = 0;
    int       max_frame_queue = 2;
    int       vsync          = 0;

    float     render_scale   = 0.67f;
    uint32_t  render_width   = 0;
    uint32_t  render_height  = 0;
    uint32_t  display_width  = 0;
    uint32_t  display_height = 0;

    float GetRenderScale() const;
    void  ApplyRenderScale();
    void  Load(const std::string& path);
    void  Save(const std::string& path);
};

Config& GetConfig();
std::string GetConfigPath();

} // namespace adrena
