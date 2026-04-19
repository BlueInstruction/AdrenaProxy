#include "adrena_core/plugin_manager.h"
#include "adrena_core/logger.h"

#include <windows.h>

#include <algorithm>
#include <cstring>
#include <mutex>

namespace adrena {

namespace {
std::mutex g_mu;

bool NameTaken(const std::vector<LoadedPlugin>& plugins, const char* name) {
    if (!name) return false;
    for (const auto& p : plugins) {
        if (p.info && p.info->name && std::strcmp(p.info->name, name) == 0) return true;
    }
    return false;
}
} // namespace

PluginManager& PluginManager::Instance() {
    static PluginManager s_instance;
    return s_instance;
}

PluginManager::~PluginManager() {
    Shutdown();
}

void PluginManager::RegisterBuiltin(const AdrenaPluginRegistration& reg) {
    std::lock_guard<std::mutex> lock(g_mu);
    if (!reg.get_info) return;

    const AdrenaPluginInfo* info = reg.get_info();
    if (!info || info->abi_version != ADRENA_PLUGIN_ABI_VERSION) {
        AD_LOG_E("PluginManager: built-in plugin has bad ABI version (%u != %u)",
                 info ? info->abi_version : 0, ADRENA_PLUGIN_ABI_VERSION);
        return;
    }
    if (NameTaken(m_plugins, info->name)) {
        AD_LOG_W("PluginManager: built-in plugin '%s' already registered; skipping",
                 info->name ? info->name : "?");
        return;
    }

    LoadedPlugin lp{};
    lp.info            = info;
    lp.upscaler_vtable = reg.get_upscaler_vtable ? reg.get_upscaler_vtable() : nullptr;
    lp.is_builtin      = true;
    m_plugins.push_back(lp);

    AD_LOG_I("PluginManager: registered built-in plugin '%s' (%s v%s)",
             info->name ? info->name : "?",
             info->display_name ? info->display_name : "?",
             info->version ? info->version : "?");
}

bool PluginManager::LoadOne(const std::string& path) {
    HMODULE h = ::LoadLibraryA(path.c_str());
    if (!h) {
        AD_LOG_W("PluginManager: failed to load '%s' (err=%lu)",
                 path.c_str(), ::GetLastError());
        return false;
    }

    auto get_info = reinterpret_cast<AdrenaPlugin_GetInfoFn>(
        ::GetProcAddress(h, ADRENA_PLUGIN_SYM_GET_INFO));
    if (!get_info) {
        AD_LOG_W("PluginManager: '%s' is missing %s; skipping",
                 path.c_str(), ADRENA_PLUGIN_SYM_GET_INFO);
        ::FreeLibrary(h);
        return false;
    }

    const AdrenaPluginInfo* info = get_info();
    if (!info || info->abi_version != ADRENA_PLUGIN_ABI_VERSION) {
        AD_LOG_W("PluginManager: '%s' ABI mismatch (got %u, want %u); skipping",
                 path.c_str(),
                 info ? info->abi_version : 0,
                 ADRENA_PLUGIN_ABI_VERSION);
        ::FreeLibrary(h);
        return false;
    }

    if (NameTaken(m_plugins, info->name)) {
        AD_LOG_W("PluginManager: plugin name '%s' already taken; skipping '%s'",
                 info->name ? info->name : "?", path.c_str());
        ::FreeLibrary(h);
        return false;
    }

    LoadedPlugin lp{};
    lp.info        = info;
    lp.dll_handle  = h;
    lp.source_path = path;
    lp.is_builtin  = false;

    if (info->kind == ADRENA_PLUGIN_UPSCALER) {
        auto get_vt = reinterpret_cast<AdrenaPlugin_GetUpscalerVTableFn>(
            ::GetProcAddress(h, ADRENA_PLUGIN_SYM_GET_UPSCALER_VTABLE));
        if (get_vt) {
            const AdrenaUpscalerVTable* vt = get_vt();
            if (vt && vt->abi_version == ADRENA_PLUGIN_ABI_VERSION &&
                vt->create && vt->init && vt->execute && vt->destroy) {
                lp.upscaler_vtable = vt;
            } else {
                AD_LOG_W("PluginManager: plugin '%s' has invalid upscaler vtable",
                         info->name ? info->name : "?");
            }
        }
    }

    m_plugins.push_back(lp);
    AD_LOG_I("PluginManager: loaded plugin '%s' (%s v%s) from %s",
             info->name ? info->name : "?",
             info->display_name ? info->display_name : "?",
             info->version ? info->version : "?",
             path.c_str());
    return true;
}

size_t PluginManager::ScanDirectory(const std::string& dir) {
    std::lock_guard<std::mutex> lock(g_mu);

    std::string pattern = dir;
    if (!pattern.empty() && pattern.back() != '\\' && pattern.back() != '/')
        pattern += "\\";
    pattern += "adrena_plugin_*.dll";

    WIN32_FIND_DATAA fd{};
    HANDLE hf = ::FindFirstFileA(pattern.c_str(), &fd);
    if (hf == INVALID_HANDLE_VALUE) return 0;

    size_t loaded = 0;
    do {
        std::string full = dir;
        if (!full.empty() && full.back() != '\\' && full.back() != '/') full += "\\";
        full += fd.cFileName;
        if (LoadOne(full)) ++loaded;
    } while (::FindNextFileA(hf, &fd));

    ::FindClose(hf);
    AD_LOG_I("PluginManager: scanned '%s' (%zu plugins loaded)",
             dir.c_str(), loaded);
    return loaded;
}

namespace {
// Free-function anchor used with GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS to
// resolve the module that owns adrena_core's code (i.e. the host DLL that
// statically linked adrena_core.a).
void PluginManagerAnchor() {}
} // namespace

size_t PluginManager::ScanDefaultDirectory() {
    char path[MAX_PATH]{};
    HMODULE self = nullptr;
    if (!::GetModuleHandleExA(
            GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
            GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCSTR>(&PluginManagerAnchor),
            &self)) {
        return 0;
    }
    if (!::GetModuleFileNameA(self, path, MAX_PATH)) return 0;

    // Strip file name.
    std::string dir = path;
    auto slash = dir.find_last_of("\\/");
    if (slash == std::string::npos) return 0;
    dir.resize(slash + 1);
    dir += "plugins";

    DWORD attr = ::GetFileAttributesA(dir.c_str());
    if (attr == INVALID_FILE_ATTRIBUTES || !(attr & FILE_ATTRIBUTE_DIRECTORY)) {
        AD_LOG_I("PluginManager: default plugin dir '%s' not found; skipping",
                 dir.c_str());
        return 0;
    }
    return ScanDirectory(dir);
}

const LoadedPlugin* PluginManager::FindByName(const std::string& name) const {
    for (const auto& p : m_plugins) {
        if (p.info && p.info->name && name == p.info->name) return &p;
    }
    return nullptr;
}

void PluginManager::Shutdown() {
    std::lock_guard<std::mutex> lock(g_mu);
    for (auto& p : m_plugins) {
        if (!p.is_builtin && p.dll_handle) {
            ::FreeLibrary(reinterpret_cast<HMODULE>(p.dll_handle));
            p.dll_handle = nullptr;
        }
    }
    m_plugins.clear();
}

} // namespace adrena
