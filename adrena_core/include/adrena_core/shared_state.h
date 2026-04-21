#pragma once
#include <windows.h>
#include <cstdint>

namespace adrena {

struct SharedState {
    // Bumped to 0x00020100 when diagnostic + Vulkan telemetry was added.
    static constexpr uint32_t VERSION_HASH = 0x00020100u;
    uint32_t version = VERSION_HASH;
    volatile long lock = 0;

    bool     sgsr_active     = false;
    bool     sgsr_enabled    = false;
    int32_t  quality_preset  = 1;

    uint32_t render_width    = 0;
    uint32_t render_height   = 0;
    uint32_t display_width   = 0;
    uint32_t display_height  = 0;

    float    sharpness       = 0.80f;
    float    render_scale    = 0.67f;

    int32_t  fg_mode         = 1;

    bool     overlay_visible = true;
    bool     fps_display     = true;

    bool     is_adreno       = false;
    int32_t  adreno_tier     = 0;

    bool     hud_horizontal  = true;   // true = bar, false = vertical

    // ── Diagnostic counters (incremented from the NVNGX proxy path) ──
    // These are the primary signal telling the user whether the proxy is
    // actually being called by the game. If `dlss_init_count` ≥ 1 but
    // `dlss_evaluate_count` stays at 0 across many frames, NGX was
    // initialised but the game never dispatched an evaluation — usually
    // because it took a non-DLSS render path or failed capability check.
    uint32_t dlss_init_count       = 0;
    uint32_t dlss_evaluate_count   = 0;
    uint32_t dlss_last_plugin_ok   = 0;   // 1 = last plugin->Execute succeeded
    uint32_t dlss_last_plugin_id   = 0;   // 0=none, 1=sgsr1, 2=sgsr2, 3=fsr2, 4=xess, 5=fsr3
    uint32_t fg_interpolate_count  = 0;   // number of synthetic frames emitted

    // Vulkan capability detection (populated by vk_fg plugin at load time)
    bool     vk_supported                 = false;
    bool     vk_shader_image_atomic_int64 = false;
    bool     vk_shader_int64              = false;
    bool     vk_shader_float16            = false;

    uint32_t reserved[4]    = {};
};

SharedState* GetSharedState();
void ReleaseSharedState();

class SharedStateLock {
public:
    explicit SharedStateLock(volatile long* lk) : m_lock(lk) {
        while (InterlockedCompareExchange(m_lock, 1, 0) != 0) {
            YieldProcessor();
        }
    }
    ~SharedStateLock() { InterlockedExchange(m_lock, 0); }
    SharedStateLock(const SharedStateLock&) = delete;
    SharedStateLock& operator=(const SharedStateLock&) = delete;
private:
    volatile long* m_lock;
};

} // namespace adrena
