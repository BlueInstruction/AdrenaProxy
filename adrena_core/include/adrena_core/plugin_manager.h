// PluginManager — loads upscaler plugins (built-in + external DLLs) and
// exposes a uniform selection interface to the host.
//
// Built-in plugins are registered at process startup via the
// AdrenaPluginRegistration struct. External plugins are discovered by
// scanning a `plugins/` directory next to the proxy DLL and loading any
// file matching `adrena_plugin_*.dll`.
#pragma once

#include "adrena_core/plugin_api.h"

#include <string>
#include <vector>

namespace adrena {

struct AdrenaPluginRegistration {
    const AdrenaPluginInfo*     (*get_info)();
    const AdrenaUpscalerVTable* (*get_upscaler_vtable)();
};

struct LoadedPlugin {
    const AdrenaPluginInfo*     info            = nullptr;
    const AdrenaUpscalerVTable* upscaler_vtable = nullptr;
    void*                       dll_handle      = nullptr;   // HMODULE or nullptr for built-in
    std::string                 source_path;                 // empty for built-in
    bool                        is_builtin      = false;
};

class PluginManager {
public:
    static PluginManager& Instance();

    /// Register a compiled-in plugin. Must be called before any
    /// FindByName / GetAll call. Thread-safe with ScanDirectory.
    void RegisterBuiltin(const AdrenaPluginRegistration& reg);

    /// Scan a directory for `adrena_plugin_*.dll` files and load them.
    /// Safe to call multiple times; duplicate names are skipped.
    /// Returns the count of newly loaded plugins.
    size_t ScanDirectory(const std::string& dir);

    /// Scan the default plugins directory (next to the host DLL, in the
    /// subdirectory `plugins/`). No-op if the directory doesn't exist.
    size_t ScanDefaultDirectory();

    /// Look up a plugin by its AdrenaPluginInfo::name. Returns nullptr if
    /// not loaded.
    const LoadedPlugin* FindByName(const std::string& name) const;

    /// Enumerate all loaded plugins, built-in first.
    const std::vector<LoadedPlugin>& GetAll() const { return m_plugins; }

    /// Unload every externally-loaded DLL. Built-in plugins are left
    /// registered because their entry-point functions live in the same
    /// module as the host.
    void Shutdown();

    PluginManager(const PluginManager&) = delete;
    PluginManager& operator=(const PluginManager&) = delete;

private:
    PluginManager() = default;
    ~PluginManager();

    bool LoadOne(const std::string& path);

    std::vector<LoadedPlugin> m_plugins;
};

} // namespace adrena
