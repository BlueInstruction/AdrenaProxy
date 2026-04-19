#include "adrena_core/shader_cache.h"
#include "adrena_core/logger.h"
#include <fstream>
#include <sstream>

namespace adrena {

ShaderCache& ShaderCache::Instance() {
    static ShaderCache inst;
    return inst;
}

ShaderCache::~ShaderCache() { Shutdown(); }

void ShaderCache::Init(const std::string& shaderDir) {
    if (m_initialized) return;
    m_shaderDir = shaderDir;

    m_changeHandle = FindFirstChangeNotificationA(
        shaderDir.c_str(),
        FALSE,                           // don't watch subtree
        FILE_NOTIFY_CHANGE_LAST_WRITE);  // only write-time changes

    if (m_changeHandle == INVALID_HANDLE_VALUE) {
        AD_LOG_W("ShaderCache: cannot watch '%s' (err %u)",
                 shaderDir.c_str(), GetLastError());
    } else {
        AD_LOG_I("ShaderCache: watching '%s' for .hlsl changes", shaderDir.c_str());
    }
    m_initialized = true;
}

void ShaderCache::Shutdown() {
    if (m_changeHandle != INVALID_HANDLE_VALUE) {
        FindCloseChangeNotification(m_changeHandle);
        m_changeHandle = INVALID_HANDLE_VALUE;
    }
    m_initialized = false;
}

bool ShaderCache::PollReload() {
    if (m_changeHandle == INVALID_HANDLE_VALUE) return false;

    DWORD result = WaitForSingleObject(m_changeHandle, 0);
    if (result == WAIT_OBJECT_0) {
        AD_LOG_I("ShaderCache: change detected in '%s'", m_shaderDir.c_str());
        // Re-arm the notification for the next change.
        FindNextChangeNotification(m_changeHandle);
        return true;
    }
    return false;
}

std::string ShaderCache::ReadShaderFile(const std::string& filename) const {
    std::string path = m_shaderDir + "\\" + filename;
    std::ifstream file(path, std::ios::in);
    if (!file.is_open()) {
        AD_LOG_E("ShaderCache: failed to read '%s'", path.c_str());
        return {};
    }
    std::ostringstream ss;
    ss << file.rdbuf();
    AD_LOG_I("ShaderCache: read '%s' (%zu bytes)", filename.c_str(), ss.str().size());
    return ss.str();
}

} // namespace adrena
