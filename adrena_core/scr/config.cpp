#include "adrena_core/config.h"
#include "adrena_core/logger.h"
#include "adrena_core/shared_state.h"
#include <fstream>
#include <sstream>
#include <algorithm>

namespace adrena {

float Config::GetRenderScale() const {
    if (custom_scale > 0.0f && custom_scale <= 1.0f) return custom_scale;
    switch (quality) {
    case Quality::UltraQuality:     return 0.77f;
    case Quality::Quality:          return 0.67f;
    case Quality::Balanced:         return 0.59f;
    case Quality::Performance:      return 0.50f;
    case Quality::UltraPerformance: return 0.33f;
    }
    return 0.67f;
}

void Config::ApplyRenderScale() {
    render_scale = GetRenderScale();
}

static std::string Trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return "";
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
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

static int ParseVKey(const std::string& s) {
    if (s == "HOME")   return VK_HOME;
    if (s == "INSERT") return VK_INSERT;
    if (s == "DELETE") return VK_DELETE;
    if (s == "END")    return VK_END;
    for (int i = 1; i <= 12; i++) {
        char buf[8]; sprintf(buf, "F%d", i);
        if (s == buf) return VK_F1 + (i - 1);
    }
    return VK_HOME;
}

static uint64_t GetFileWriteTime(const std::string& path) {
    WIN32_FILE_ATTRIBUTE_DATA attr{};
    if (!GetFileAttributesExA(path.c_str(), GetFileExInfoStandard, &attr))
        return 0;
    return ((uint64_t)attr.ftLastWriteTime.dwHighDateTime << 32)
         | (uint64_t)attr.ftLastWriteTime.dwLowDateTime;
}

bool Config::PollReload() {
    if (m_loadedPath.empty()) return false;
    uint64_t wt = GetFileWriteTime(m_loadedPath);
    if (wt != 0 && wt != m_lastWriteTime) {
        AD_LOG_I("Config: INI changed on disk, reloading");
        Load(m_loadedPath);
        return true;
    }
    return false;
}

// Scan just enough of the INI to pick out a top-level `profile = <name>`
// directive (outside of any [section]).  Used by Load() to overlay a
// per-environment preset from profiles/<name>.ini before the user INI.
static std::string ScanProfileName(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) return "";
    std::string line, section;
    while (std::getline(file, line)) {
        line = Trim(line);
        if (line.empty() || line[0] == ';' || line[0] == '#') continue;
        if (line[0] == '[') {
            auto e = line.find(']');
            if (e != std::string::npos) section = line.substr(1, e - 1);
            continue;
        }
        if (!section.empty()) continue;  // only the preamble counts
        auto eq = line.find('=');
        if (eq == std::string::npos) continue;
        if (Trim(line.substr(0, eq)) == "profile") return Trim(line.substr(eq + 1));
    }
    return "";
}

static std::string DirOf(const std::string& path) {
    auto p = path.find_last_of("\\/");
    return (p == std::string::npos) ? "" : path.substr(0, p + 1);
}

// Resolve a profile name to an absolute file path.  Search order:
//   1. <userIniDir>/profiles/<name>.ini
//   2. <hostDllDir>/profiles/<name>.ini   (same folder as adrena_proxy.ini)
// Returns empty string if the file doesn't exist in either location.
static std::string ResolveProfilePath(const std::string& name, const std::string& userIni) {
    if (name.empty()) return "";
    std::string rel = "profiles\\" + name + ".ini";
    std::string c1 = DirOf(userIni) + rel;
    std::ifstream f1(c1);
    if (f1.good()) return c1;
    char exePath[MAX_PATH]{};
    GetModuleFileNameA(nullptr, exePath, MAX_PATH);
    std::string c2 = DirOf(exePath) + rel;
    std::ifstream f2(c2);
    if (f2.good()) return c2;
    return "";
}

void Config::Load(const std::string& path) {
    m_loadedPath = path;
    m_lastWriteTime = GetFileWriteTime(path);

    // Two-pass loading: if the user INI names a profile, consume the
    // profile first (without touching m_loadedPath so hot-reload keeps
    // watching the user file) and then overlay the user INI on top.
    std::string profileName = ScanProfileName(path);
    if (!profileName.empty()) {
        std::string profilePath = ResolveProfilePath(profileName, path);
        if (!profilePath.empty()) {
            AD_LOG_I("Config: overlaying profile '%s' from %s",
                     profileName.c_str(), profilePath.c_str());
            // Recurse, but preserve the user path as the watched file.
            std::string userPath = m_loadedPath;
            uint64_t    userMTime= m_lastWriteTime;
            Load(profilePath);
            m_loadedPath    = userPath;
            m_lastWriteTime = userMTime;
        } else {
            AD_LOG_W("Config: profile '%s' not found in profiles/ — ignoring",
                     profileName.c_str());
        }
        profile = profileName;
    }

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
        try {
        // Top-level (pre-[section]) directives.
        if (section.empty()) {
            if (k == "profile") profile = v;
            continue;
        }
        if (section == "SGSR") {
            if (k == "enabled")       enabled = (std::stoi(v) != 0);
            else if (k == "mode")     sgsr_mode = ParseSGSRMode(v);
            else if (k == "quality")  quality = ParseQuality(v);
            else if (k == "custom_scale") custom_scale = std::stof(v);
            else if (k == "sharpness") sharpness = std::stof(v);
        } else if (section == "FrameGeneration") {
            if (k == "mode")           fg_mode = ParseFGMode(v);
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
        }
        } catch (...) {
            AD_LOG_W("Config: malformed value for [%s] %s", section.c_str(), k.c_str());
        }
    }
    ApplyRenderScale();
    AD_LOG_I("Config loaded: SGSR=%d Quality=%d Scale=%.2f FG=%d",
             (int)sgsr_mode, (int)quality, render_scale, (int)fg_mode);
}

void Config::Save(const std::string& path) {
    std::ofstream file(path);
    if (!file.is_open()) return;
    file << "; AdrenaProxy v2.0 Configuration\n\n";
    file << "[SGSR]\n";
    file << "enabled=" << (enabled?1:0) << "\n";
    file << "mode=" << (sgsr_mode==SGSRMode::SGSR1?"sgsr1":sgsr_mode==SGSRMode::SGSR2?"sgsr2":"off") << "\n";
    file << "quality=";
    switch(quality) {
        case Quality::UltraQuality: file << "ultra_quality"; break;
        case Quality::Quality: file << "quality"; break;
        case Quality::Balanced: file << "balanced"; break;
        case Quality::Performance: file << "performance"; break;
        case Quality::UltraPerformance: file << "ultra_performance"; break;
    }
    file << "\ncustom_scale=" << custom_scale << "\n";
    file << "sharpness=" << sharpness << "\n\n";
    file << "[FrameGeneration]\nmode=x" << (int)fg_mode+1 << "\n";
    file << "fps_threshold=" << fps_threshold << "\n\n";
    file << "[Overlay]\nenabled=" << (overlay_enabled?1:0);
    file << "\ntoggle_key=HOME\nfps_display=" << (fps_display?1:0);
    file << "\nopacity=" << overlay_opacity << "\n\n";
    file << "[Advanced]\nforce_d3d11=" << (force_d3d11?1:0);
    file << "\nrt_format=" << rt_format << "\nmax_frame_queue=" << max_frame_queue;
    file << "\nvsync=" << vsync << "\n";
}

std::string GetConfigPath() {
    char exePath[MAX_PATH];
    GetModuleFileNameA(nullptr, exePath, MAX_PATH);
    std::string path(exePath);
    auto pos = path.find_last_of("\\/");
    if (pos != std::string::npos) path = path.substr(0, pos + 1);
    path += "adrena_proxy.ini";
    return path;
}

Config& GetConfig() {
    static Config cfg;
    static bool loaded = false;
    if (!loaded) {
        cfg.Load(GetConfigPath());
        loaded = true;
        SharedState* ss = GetSharedState();
        if (ss) {
            SharedStateLock lock(&ss->lock);
            ss->fg_mode = static_cast<int32_t>(cfg.fg_mode) + 1;
            ss->overlay_visible = cfg.overlay_enabled;
            ss->sharpness = cfg.sharpness;
            ss->render_scale = cfg.render_scale;
        }
    }
    return cfg;
}

} // namespace adrena
