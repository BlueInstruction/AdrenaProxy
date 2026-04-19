#pragma once
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>
#include <windows.h>
#include <d3d12.h>
#include <dxgi.h>
#include "adrena_core/plugin_api.h"

namespace adrena {

class IPlugin {
public:
    virtual ~IPlugin() = default;
    virtual const AdrenaPluginDesc& Desc()         const = 0;
    virtual bool                    IsInitialized() const = 0;
    virtual bool Init(ID3D12Device* dev, DXGI_FORMAT fmt,
                      uint32_t rW, uint32_t rH,
                      uint32_t dW, uint32_t dH)          = 0;
    virtual bool Resize(uint32_t rW, uint32_t rH,
                        uint32_t dW, uint32_t dH)         = 0;
    virtual bool Execute(ID3D12GraphicsCommandList* cl,
                         const AdrenaUpscaleParams& p)    = 0;
    virtual void Shutdown()                               = 0;
};

class PluginManager {
public:
    static PluginManager& Get();

    void RegisterBuiltins();
    void LoadExternalPlugins(const wchar_t* pluginDir);
    void UnloadAll();

    IPlugin*                              Find(const char* id) const;
    std::vector<const AdrenaPluginDesc*>  ListAll()            const;

private:
    PluginManager()  = default;
    ~PluginManager();

    struct LoadedDll {
        HMODULE                handle;
        std::unique_ptr<IPlugin> plugin;
    };

    std::unordered_map<std::string, std::unique_ptr<IPlugin>> m_builtins;
    std::vector<LoadedDll>                                     m_external;
};

} // namespace adrena
