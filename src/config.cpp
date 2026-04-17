#include "config.h"
#include "logger.h"
#include <windows.h>
#include <fstream>
#include <sstream>

namespace adrena {

float Config::GetRenderScale() const
{
    if (custom_scale > 0.0f && custom_scale <= 1.0f)
        return custom_scale;
    switch (quality) {
    case Quality::UltraQuality:      return 0.77f;
    case Quality::Quality:           return 0.67f;
    case Quality::Balanced:          return 0.59f;
    case Quality::Performance:       return 0.50f;
    case Quality::UltraPerformance:  return 0.33f;
    }
    return 0.67f;
}

void Config::ApplyRenderScale()
{
    render_scale = GetRenderScale();
}

static SGSRMode ParseSGSRMode(const std::string& s) {
    if (s == "sgsr1") return SGSRMode::SGSR1;
    if (s == "sgsr2") return SGSRMode::SGSR2;
    return SGSRMode::Off;
}
static Quality ParseQuality(const std::string& s) {
    if (s == "ultra_quality")     return Quality::UltraQuality;
    if (s == "quality")           return Quality::Quality;
    if (s == "balanced")          return Quality::Balanced;
    if (s == "performance")       return Quality::Performance;
    if (s == "ultra_performance") return Quality::UltraPerformance;
    return Quality::Quality;
}
static FGMode ParseFGMode(const std::string& s) {
    if (s == "x2") return FGMode::X2;
    if (s == "x3") return FGMode::X3;
    if (s == "x4") return FGMode::X4;
    return FGMode::X1;
}
static MotionQuality ParseMotionQuality(const std::string& s) {
    if (s == "high")   return MotionQuality::High;
    if (s == "medium") return MotionQuality::Medium;
    return MotionQuality::Low;
}
static int ParseVKey(const std::string& s) {
    if (s == "HOME")      return VK_HOME;
    if (s == "INSERT")    return VK_INSERT;
    if (s == "DELETE")    return VK_DELETE;
    if (s == "END")       return VK_END;
    if (s == "PAGE_UP")   return VK_PRIOR;
    if (s == "PAGE_DOWN") return VK_NEXT;
    for (int i = 1; i <= 12; i++) {
        char buf[8]; sprintf(buf, "F%d", i);
        if (s == buf) return VK_F1 + (i - 1);
    }
    return VK_HOME;
}
static std::string Trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return "";
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

void Config::Load(const std::string& path)
{
    std::ifstream file(path);
    if (!file.is_open()) {
        AD_LOG_W("Config not found: %s — using defaults", path.c_str());
        ApplyRenderScale();
        return;
    }
    std::string section, line;
    while (std::getline(file, line)) {
        line = Trim(line);
        if (line.empty() || line[0] == ';' || line[0] == '#') continue;
        if (line[0] == '[') {
            auto e = line.find(']');
            if (e != std::string::npos) section = line.substr(1, e - 1);
            continue;
        }
        auto eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string k = Trim(line.substr(0, eq));
        std::string v = Trim(line.substr(eq + 1));

        if (section == "SGSR") {
            if (k == "enabled")       enabled = (std::stoi(v) != 0);
            else if (k == "mode")     sgsr_mode = ParseSGSRMode(v);
            else if (k == "quality")  quality = ParseQuality(v);
            else if (k == "custom_scale") custom_scale = std::stof(v);
            else if (k == "sharpness") sharpness = std::stof(v);
        } else if (section == "FrameGeneration") {
            if (k == "mode")           fg_mode = ParseFGMode(v);
            else if (k == "motion_quality") motion_quality = ParseMotionQuality(v);
            else if (k == "fps_threshold") fps_threshold = std::stoi(v);
        } else if (section == "Overlay") {
            if (k == "enabled")        overlay_enabled = (std::stoi(v) != 0);
            else if (k == "toggle_key") toggle_key = ParseVKey(v);
            else if (k == "fps_display") fps_display = (std::stoi(v) != 0);
            else if (k == "opacity")   overlay_opacity = std::stof(v);
        } else if (section == "Advanced") {
            if (k == "force_d3d11")    force_d3d11 = (std::stoi(v) != 0);
            else if (k == "rt_format") rt_format = std::stoi(v);
            else if (k == "max_frame_queue") max_frame_queue = std::stoi(v);
            else if (k == "vsync")     vsync = std::stoi(v);
            else if (k == "auto_detect_gpu") auto_detect_gpu = (std::stoi(v) != 0);
        }
    }
    ApplyRenderScale();
    AD_LOG_I("Config loaded: SGSR=%d Quality=%d Scale=%.2f FG=%d",
             (int)sgsr_mode, (int)quality, render_scale, (int)fg_mode);
}

void Config::Save(const std::string& path)
{
    std::ofstream file(path);
    if (!file.is_open()) return;
    file << "; AdrenaProxy Configuration — Auto-saved\n\n";
    file << "[SGSR]\n";
    file << "enabled=" << (enabled?1:0) << "\n";
    file << "mode=" << (sgsr_mode==SGSRMode::SGSR1?"sgsr1":sgsr_mode==SGSRMode::SGSR2?"sgsr2":"off") << "\n";
    file << "quality=" << (quality==Quality::UltraQuality?"ultra_quality":quality==Quality::Quality?"quality":quality==Quality::Balanced?"balanced":quality==Quality::Performance?"performance":"ultra_performance") << "\n";
    file << "custom_scale=" << custom_scale << "\n";
    file << "sharpness=" << sharpness << "\n\n";
    file << "[FrameGeneration]\nmode=x" << (int)fg_mode+1 << "\nmotion_quality="
         << (motion_quality==MotionQuality::High?"high":motion_quality==MotionQuality::Medium?"medium":"low")
         << "\nfps_threshold=" << fps_threshold << "\n\n";
    file << "[Overlay]\nenabled=" << (overlay_enabled?1:0)
         << "\ntoggle_key=HOME"
         << "\nfps_display=" << (fps_display?1:0)
         << "\nopacity=" << overlay_opacity << "\n\n";
    file << "[Advanced]\nforce_d3d11=" << (force_d3d11?1:0)
         << "\nrt_format=" << rt_format
         << "\nmax_frame_queue=" << max_frame_queue
         << "\nvsync=" << vsync
         << "\nauto_detect_gpu=" << (auto_detect_gpu?1:0) << "\n";
}

Config& GetConfig() {
    static Config cfg;
    return cfg;
}

} // namespace adrena