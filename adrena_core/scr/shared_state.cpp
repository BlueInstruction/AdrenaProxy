#include "adrena_core/shared_state.h"
#include "adrena_core/logger.h"
#include <windows.h>
#include <cstdio>

namespace adrena {

static const char* GetSharedStateName() {
    static char nameBuf[128]{};
    if (nameBuf[0] == '\0') {
        snprintf(nameBuf, sizeof(nameBuf), "Local\\AdrenaProxy_v2_%lu", GetCurrentProcessId());
    }
    return nameBuf;
}

SharedState* GetSharedState() {
    static SharedState* s_state = nullptr;
    static HANDLE s_hMap = nullptr;
    if (s_state) return s_state;
    const char* name = GetSharedStateName();
    s_hMap = OpenFileMappingA(FILE_MAP_ALL_ACCESS, FALSE, name);
    if (s_hMap) {
        s_state = (SharedState*)MapViewOfFile(s_hMap, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(SharedState));
        if (s_state) AD_LOG_I("SharedState: opened existing (%s)", name);
        else { AD_LOG_E("SharedState: MapViewOfFile failed"); CloseHandle(s_hMap); s_hMap = nullptr; }
        return s_state;
    }
    s_hMap = CreateFileMappingA(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, sizeof(SharedState), name);
    if (!s_hMap) { AD_LOG_E("SharedState: CreateFileMapping failed"); return nullptr; }
    bool created = (GetLastError() != ERROR_ALREADY_EXISTS);
    s_state = (SharedState*)MapViewOfFile(s_hMap, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(SharedState));
    if (!s_state) { AD_LOG_E("SharedState: MapViewOfFile failed on new"); CloseHandle(s_hMap); s_hMap = nullptr; return nullptr; }
    if (created) {
        SharedState zeroed{};
        zeroed.version = SharedState::VERSION_HASH;
        *s_state = zeroed;
        AD_LOG_I("SharedState: created new (%s)", name);
    } else {
        AD_LOG_I("SharedState: opened race-created (%s)", name);
    }
    return s_state;
}

void ReleaseSharedState() {
    AD_LOG_I("SharedState: release called (no-op for safety)");
}

} // namespace adrena