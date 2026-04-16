#pragma once
#include <string>
#include <cstdint>

namespace adrena {

enum class SGSRMode     { Off, SGSR1, SGSR2 };
enum class Quality      { UltraQuality, Quality, Balanced, Performance, UltraPerformance };
enum class FGMode       { X1, X2, X3, X4 };
enum class MotionQuality{ Low, Medium, High };

struct Config {
    // SGSR
    bool      enabled       = true;
    SGSRMode  sgsr_mode     = SGSRMode::SGSR1;
    Quality   quality       = Quality::Quality;
    float     custom_scale  = 0.0f;
    float     sharpness     = 0.80f;

    // Frame Generation
    FGMode     fg_mode        = FGMode::X1;
    MotionQuality motion_quality = MotionQuality::Medium;
    int        fps_threshold  = 60;

    // Overlay
    bool      overlay_enabled = true;
    int       toggle_key      = VK_HOME;
    bool      fps_display     = true;
    float     overlay_opacity = 0.85f;

    // Advanced
    bool      force_d3d11     = false;
    int       rt_format       = 0;
    int       max_frame_queue = 2;
    int       vsync           = 0;
    bool      auto_detect_gpu = true;

    // Runtime
    float     render_scale    = 0.67f;

    float GetRenderScale() const;
    void  ApplyRenderScale();
    void  Load(const std::string& path);
    void  Save(const std::string& path);
};

Config& GetConfig();

} // namespace adrena