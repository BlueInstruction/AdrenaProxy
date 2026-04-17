#pragma once
#include <windows.h>
#include <cstdint>

namespace adrena {

struct SharedState {
    static constexpr uint32_t VERSION_HASH = 0x00020000u;
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

    uint32_t reserved[16]    = {};
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