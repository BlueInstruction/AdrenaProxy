#include "adrena_core/plugin_manager.h"
#include "adrena_core/logger.h"
#include "adrena_core/sgsr1_pass.h"
#include "adrena_core/sgsr2_pass.h"
#include <algorithm>
#include <cstdio>
#include <cstring>

namespace adrena {

// ── Built-in SGSR1 ────────────────────────────────────────────────────────

class BuiltinSGSR1Plugin final : public IPlugin {
public:
    const AdrenaPluginDesc& Desc()         const override { return s_desc; }
    bool                    IsInitialized() const override { return m_pass.IsInitialized(); }

    bool Init(ID3D12Device* dev, DXGI_FORMAT fmt,
              uint32_t rW, uint32_t rH, uint32_t dW, uint32_t dH) override {
        return m_pass.Init(dev, fmt, rW, rH, dW, dH);
    }
    bool Resize(uint32_t rW, uint32_t rH, uint32_t dW, uint32_t dH) override {
        return m_pass.Resize(rW, rH, dW, dH);
    }
    bool Execute(ID3D12GraphicsCommandList* cl, const AdrenaUpscaleParams& p) override {
        SGSRParams sp{};
        sp.color         = p.color;
        sp.depth         = p.depth;
        sp.motion        = p.motion;
        sp.output        = p.output;
        sp.sharpness     = p.sharpness;
        sp.renderWidth   = p.renderWidth;
        sp.renderHeight  = p.renderHeight;
        sp.displayWidth  = p.displayWidth;
        sp.displayHeight = p.displayHeight;
        m_pass.Execute(cl, sp);
        return true;
    }
    void Shutdown() override { m_pass.Shutdown(); }

private:
    SGSR1Pass m_pass;
    static const AdrenaPluginDesc s_desc;
};

const AdrenaPluginDesc BuiltinSGSR1Plugin::s_desc = {
    ADRENA_PLUGIN_API_VERSION, "sgsr1", "Snapdragon GSR 1 (Spatial)", "1.0.0", 0, 0, 0
};

// ── Built-in SGSR2 ────────────────────────────────────────────────────────

#ifdef ADRENA_SGSR2_ENABLED
class BuiltinSGSR2Plugin final : public IPlugin {
public:
    const AdrenaPluginDesc& Desc()         const override { return s_desc; }
    bool                    IsInitialized() const override { return m_pass.IsInitialized(); }

    bool Init(ID3D12Device* dev, DXGI_FORMAT fmt,
              uint32_t rW, uint32_t rH, uint32_t dW, uint32_t dH) override {
        return m_pass.Init(dev, fmt, rW, rH, dW, dH);
    }
    bool Resize(uint32_t rW, uint32_t rH, uint32_t dW, uint32_t dH) override {
        return m_pass.Resize(rW, rH, dW, dH);
    }
    bool Execute(ID3D12GraphicsCommandList* cl, const AdrenaUpscaleParams& p) override {
        SGSR2Params sp{};
        sp.color               = p.color;
        sp.depth               = p.depth;
        sp.motion              = p.motion;
        sp.output              = p.output;
        sp.sharpness           = p.sharpness;
        sp.renderWidth         = p.renderWidth;
        sp.renderHeight        = p.renderHeight;
        sp.displayWidth        = p.displayWidth;
        sp.displayHeight       = p.displayHeight;
        sp.resetHistory        = p.resetHistory != 0;
        sp.preExposure         = p.preExposure;
        sp.minLerpContribution = p.minLerpContribution;
        sp.bSameCamera         = p.sameCamera != 0;
        sp.cameraFovAngleHor   = p.cameraFovH;
        sp.cameraNear          = p.cameraNear;
        sp.cameraFar           = p.cameraFar;
        m_pass.Execute(cl, sp);
        return true;
    }
    void Shutdown() override { m_pass.Shutdown(); }

private:
    SGSR2Pass m_pass;
    static const AdrenaPluginDesc s_desc;
};

const AdrenaPluginDesc BuiltinSGSR2Plugin::s_desc = {
    ADRENA_PLUGIN_API_VERSION, "sgsr2", "Snapdragon GSR 2 (Temporal)", "1.0.0", 1, 1, 1
};
#endif

// ── External plugin adapter ───────────────────────────────────────────────

class ExternalPlugin final : public IPlugin {
public:
    ExternalPlugin(AdrenaPluginCtx          ctx,
                   PFN_AdrenaPlugin_GetDesc  fnGetDesc,
                   PFN_AdrenaPlugin_Destroy  fnDestroy,
                   PFN_AdrenaPlugin_Init     fnInit,
                   PFN_AdrenaPlugin_Resize   fnResize,
                   PFN_AdrenaPlugin_Execute  fnExecute,
                   PFN_AdrenaPlugin_Shutdown fnShutdown)
        : m_ctx(ctx)
        , m_desc(*fnGetDesc())
        , m_fnDestroy(fnDestroy)
        , m_fnInit(fnInit)
        , m_fnResize(fnResize)
        , m_fnExecute(fnExecute)
        , m_fnShutdown(fnShutdown)
        , m_initialized(false)
    {}

    ~ExternalPlugin() override {
        if (m_initialized) m_fnShutdown(m_ctx);
        m_fnDestroy(m_ctx);
    }

    const AdrenaPluginDesc& Desc()         const override { return m_desc; }
    bool                    IsInitialized() const override { return m_initialized; }

    bool Init(ID3D12Device* dev, DXGI_FORMAT fmt,
              uint32_t rW, uint32_t rH, uint32_t dW, uint32_t dH) override {
        m_initialized = (m_fnInit(m_ctx, dev, fmt, rW, rH, dW, dH) == 0);
        return m_initialized;
    }
    bool Resize(uint32_t rW, uint32_t rH, uint32_t dW, uint32_t dH) override {
        return m_fnResize(m_ctx, rW, rH, dW, dH) == 0;
    }
    bool Execute(ID3D12GraphicsCommandList* cl, const AdrenaUpscaleParams& p) override {
        return m_fnExecute(m_ctx, cl, &p) == 0;
    }
    void Shutdown() override {
        if (m_initialized) {
            m_fnShutdown(m_ctx);
            m_initialized = false;
        }
    }

private:
    AdrenaPluginCtx           m_ctx;
    AdrenaPluginDesc          m_desc;
    PFN_AdrenaPlugin_Destroy  m_fnDestroy;
    PFN_AdrenaPlugin_Init     m_fnInit;
    PFN_AdrenaPlugin_Resize   m_fnResize;
    PFN_AdrenaPlugin_Execute  m_fnExecute;
    PFN_AdrenaPlugin_Shutdown m_fnShutdown;
    bool                      m_initialized;
};

// ── PluginManager ─────────────────────────────────────────────────────────

PluginManager& PluginManager::Get() {
    static PluginManager instance;
    return instance;
}

PluginManager::~PluginManager() {
    UnloadAll();
}

void PluginManager::RegisterBuiltins() {
    m_builtins["sgsr1"] = std::make_unique<BuiltinSGSR1Plugin>();
#ifdef ADRENA_SGSR2_ENABLED
    m_builtins["sgsr2"] = std::make_unique<BuiltinSGSR2Plugin>();
#endif
}

void PluginManager::LoadExternalPlugins(const wchar_t* pluginDir) {
    wchar_t pattern[MAX_PATH];
    swprintf(pattern, MAX_PATH, L"%s\\*.adrena.dll", pluginDir);

    WIN32_FIND_DATAW fd{};
    HANDLE hFind = FindFirstFileW(pattern, &fd);
    if (hFind == INVALID_HANDLE_VALUE) return;

    do {
        wchar_t path[MAX_PATH];
        swprintf(path, MAX_PATH, L"%s\\%s", pluginDir, fd.cFileName);

        HMODULE hMod = LoadLibraryW(path);
        if (!hMod) {
            AD_LOG_W("PluginManager: failed to load plugin (err %u)", GetLastError());
            continue;
        }

#define LOAD_SYM(type, name) \
        auto fn##name = reinterpret_cast<type>( \
            GetProcAddress(hMod, ADRENA_PLUGIN_EXPORT_SYMBOL_##name)); \
        if (!fn##name) { \
            AD_LOG_W("PluginManager: missing symbol " ADRENA_PLUGIN_EXPORT_SYMBOL_##name); \
            FreeLibrary(hMod); \
            continue; \
        }

        LOAD_SYM(PFN_AdrenaPlugin_GetDesc,  GetDesc)
        LOAD_SYM(PFN_AdrenaPlugin_Create,   Create)
        LOAD_SYM(PFN_AdrenaPlugin_Destroy,  Destroy)
        LOAD_SYM(PFN_AdrenaPlugin_Init,     Init)
        LOAD_SYM(PFN_AdrenaPlugin_Resize,   Resize)
        LOAD_SYM(PFN_AdrenaPlugin_Execute,  Execute)
        LOAD_SYM(PFN_AdrenaPlugin_Shutdown, Shutdown)
#undef LOAD_SYM

        const AdrenaPluginDesc* desc = fnGetDesc();
        if (!desc || desc->apiVersion != ADRENA_PLUGIN_API_VERSION) {
            AD_LOG_W("PluginManager: incompatible API version in plugin");
            FreeLibrary(hMod);
            continue;
        }

        bool duplicate = m_builtins.count(desc->id) != 0;
        if (!duplicate) {
            for (const auto& e : m_external) {
                if (e.plugin && std::strcmp(e.plugin->Desc().id, desc->id) == 0) {
                    duplicate = true;
                    break;
                }
            }
        }
        if (duplicate) {
            AD_LOG_W("PluginManager: duplicate id '%s', skipping", desc->id);
            FreeLibrary(hMod);
            continue;
        }

        AdrenaPluginCtx ctx = fnCreate();
        if (!ctx) {
            AD_LOG_W("PluginManager: Create() returned null");
            FreeLibrary(hMod);
            continue;
        }

        auto plugin = std::make_unique<ExternalPlugin>(
            ctx, fnGetDesc, fnDestroy, fnInit, fnResize, fnExecute, fnShutdown);
        AD_LOG_I("PluginManager: loaded '%s' v%s", desc->id, desc->version);
        m_external.push_back({ hMod, std::move(plugin) });

    } while (FindNextFileW(hFind, &fd));

    FindClose(hFind);
}

void PluginManager::UnloadAll() {
    for (auto& e : m_external) {
        e.plugin.reset();
        if (e.handle) FreeLibrary(e.handle);
    }
    m_external.clear();
    m_builtins.clear();
}

IPlugin* PluginManager::Find(const char* id) const {
    auto it = m_builtins.find(id);
    if (it != m_builtins.end()) return it->second.get();
    for (const auto& e : m_external)
        if (e.plugin && std::strcmp(e.plugin->Desc().id, id) == 0)
            return e.plugin.get();
    return nullptr;
}

std::vector<const AdrenaPluginDesc*> PluginManager::ListAll() const {
    std::vector<const AdrenaPluginDesc*> out;
    out.reserve(m_builtins.size() + m_external.size());
    for (const auto& kv : m_builtins)
        out.push_back(&kv.second->Desc());
    for (const auto& e : m_external)
        if (e.plugin) out.push_back(&e.plugin->Desc());
    return out;
}

} // namespace adrena
