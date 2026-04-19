// Tiny helper that lazy-loads an SDK runtime DLL at plugin create() time
// and caches the HMODULE. Plugins use this so they can be built without a
// compile-time dependency on the SDK and fall back gracefully when the
// SDK runtime isn't present next to the game.
#pragma once

#include <windows.h>
#include <string>

namespace adrena::plugin {

class SdkLoader {
public:
    SdkLoader() = default;
    ~SdkLoader() { Unload(); }

    SdkLoader(const SdkLoader&)            = delete;
    SdkLoader& operator=(const SdkLoader&) = delete;

    // Try each candidate DLL name in order. Returns true if one loaded.
    template <size_t N>
    bool TryLoadAny(const char* const (&names)[N]) {
        for (const char* n : names) {
            if (!n) continue;
            if (HMODULE h = ::LoadLibraryA(n)) {
                m_handle = h;
                m_name   = n;
                return true;
            }
        }
        return false;
    }

    bool     IsLoaded() const { return m_handle != nullptr; }
    HMODULE  Handle()   const { return m_handle; }
    const std::string& Name() const { return m_name; }

    template <typename Fn>
    Fn GetProc(const char* sym) const {
        return m_handle ? reinterpret_cast<Fn>(::GetProcAddress(m_handle, sym))
                        : nullptr;
    }

    void Unload() {
        if (m_handle) { ::FreeLibrary(m_handle); m_handle = nullptr; m_name.clear(); }
    }

private:
    HMODULE     m_handle = nullptr;
    std::string m_name;
};

} // namespace adrena::plugin
