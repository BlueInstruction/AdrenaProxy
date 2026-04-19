#pragma once
#include <windows.h>
#include <string>
#include <cstdint>

namespace adrena {

// Watches the shaders/ directory for .hlsl changes and provides
// a polling interface for hot-reload.
class ShaderCache {
public:
    static ShaderCache& Instance();

    // Call once at startup with the directory containing .hlsl files.
    void Init(const std::string& shaderDir);
    void Shutdown();

    // Returns true (once) when any file in the watched directory changed.
    // Non-blocking — uses FindFirstChangeNotification + WaitForSingleObject(0).
    bool PollReload();

    // Reads a shader file from disk (for runtime recompilation).
    // Returns empty string on failure.
    std::string ReadShaderFile(const std::string& filename) const;

    const std::string& GetShaderDir() const { return m_shaderDir; }

private:
    ShaderCache() = default;
    ~ShaderCache();
    ShaderCache(const ShaderCache&) = delete;
    ShaderCache& operator=(const ShaderCache&) = delete;

    std::string m_shaderDir;
    HANDLE      m_changeHandle = INVALID_HANDLE_VALUE;
    bool        m_initialized  = false;
};

} // namespace adrena
