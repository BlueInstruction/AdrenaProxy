// adrenaproxy_sgsr.dll — NVNGX / DLSS proxy entry points.
//
// Games call into this DLL expecting DLSS; we route the evaluate call
// through PluginManager so the active upscaler (SGSR1, SGSR2, FSR, XeSS,
// custom DLL plugin, …) is selected at runtime from config.

#include "ngx_param.h"
#include <adrena_core/config.h>
#include <adrena_core/logger.h>
#include <adrena_core/gpu_detect.h>
#include <adrena_core/shared_state.h>
#include <adrena_core/plugin_api.h>
#include <adrena_core/plugin_manager.h>
#include <adrena_core/builtin_plugins.h>

#include <windows.h>
#include <d3d12.h>

#include <cstdint>
#include <cstring>
#include <string>

static constexpr int NVNGX_SUCCESS = 0x1;
static constexpr int NVNGX_FAIL    = 0x0;

// ───────────────────────── Process state ─────────────────────────

static bool                    g_initialized   = false;
static ID3D12Device*           g_device        = nullptr;
static adrena::NGXParameter*   g_params        = nullptr;
static bool                    g_historyValid  = false;

// Currently-active plugin + its instance.
static const adrena::LoadedPlugin*  g_activePlugin = nullptr;
static AdrenaUpscalerContext*       g_activeCtx    = nullptr;
static std::string                  g_activeName;    // cached for reselection
static uint32_t                     g_currentRenderW = 0, g_currentRenderH = 0;
static uint32_t                     g_currentDisplayW = 0, g_currentDisplayH = 0;

// Resolve the name of the plugin that should be active based on config.
// Empty string means "upscaling disabled".
static std::string ResolvePluginName(const adrena::Config& cfg) {
    if (!cfg.upscaler_plugin.empty()) return cfg.upscaler_plugin;
    switch (cfg.sgsr_mode) {
    case adrena::SGSRMode::SGSR1: return "sgsr1";
    case adrena::SGSRMode::SGSR2: return "sgsr2";
    case adrena::SGSRMode::Off:   return {};
    }
    return {};
}

static void TearDownActive() {
    if (g_activePlugin && g_activePlugin->upscaler_vtable && g_activeCtx) {
        g_activePlugin->upscaler_vtable->destroy(g_activeCtx);
    }
    g_activeCtx    = nullptr;
    g_activePlugin = nullptr;
    g_activeName.clear();
    g_historyValid = false;
}

// Select + (re)initialize the active upscaler plugin for the given extents.
// Returns true if an upscaler is now ready to run.
static bool EnsureActivePlugin(uint32_t renderW, uint32_t renderH,
                               uint32_t displayW, uint32_t displayH,
                               DXGI_FORMAT outputFormat) {
    adrena::Config& cfg = adrena::GetConfig();
    const std::string desired = ResolvePluginName(cfg);
    if (desired.empty() || !g_device) {
        TearDownActive();
        return false;
    }

    auto& pm = adrena::PluginManager::Instance();

    // Selection changed → rebuild.
    if (g_activeName != desired) {
        TearDownActive();
        g_activePlugin = pm.FindByName(desired);
        if (!g_activePlugin) {
            AD_LOG_W("DLSS proxy: plugin '%s' not found in PluginManager",
                     desired.c_str());
            return false;
        }
        if (!g_activePlugin->upscaler_vtable) {
            AD_LOG_W("DLSS proxy: plugin '%s' is not an upscaler",
                     desired.c_str());
            g_activePlugin = nullptr;
            return false;
        }
        g_activeName = desired;
        g_activeCtx  = g_activePlugin->upscaler_vtable->create(
            g_device, ADRENA_GFX_D3D12);
        if (!g_activeCtx) {
            AD_LOG_E("DLSS proxy: plugin '%s' create() failed",
                     desired.c_str());
            g_activePlugin = nullptr;
            g_activeName.clear();
            return false;
        }
        const int rc = g_activePlugin->upscaler_vtable->init(
            g_activeCtx, static_cast<uint32_t>(outputFormat),
            renderW, renderH, displayW, displayH);
        if (rc != ADRENA_PLUGIN_OK) {
            AD_LOG_E("DLSS proxy: plugin '%s' init() -> %d",
                     desired.c_str(), rc);
            TearDownActive();
            return false;
        }
        g_currentRenderW  = renderW;  g_currentRenderH  = renderH;
        g_currentDisplayW = displayW; g_currentDisplayH = displayH;
        g_historyValid    = false;
        AD_LOG_I("DLSS proxy: activated plugin '%s' (%ux%u -> %ux%u)",
                 desired.c_str(), renderW, renderH, displayW, displayH);
        return true;
    }

    // Same plugin, extents changed → resize (or reinit if resize unsupported).
    if (renderW  != g_currentRenderW  || renderH  != g_currentRenderH ||
        displayW != g_currentDisplayW || displayH != g_currentDisplayH) {
        int rc = ADRENA_PLUGIN_E_UNSUPPORTED_API;
        if (g_activePlugin->upscaler_vtable->resize) {
            rc = g_activePlugin->upscaler_vtable->resize(
                g_activeCtx, renderW, renderH, displayW, displayH);
        }
        if (rc == ADRENA_PLUGIN_E_UNSUPPORTED_API) {
            // Recreate.
            g_activePlugin->upscaler_vtable->destroy(g_activeCtx);
            g_activeCtx = g_activePlugin->upscaler_vtable->create(
                g_device, ADRENA_GFX_D3D12);
            if (!g_activeCtx) { g_activePlugin = nullptr; g_activeName.clear(); return false; }
            rc = g_activePlugin->upscaler_vtable->init(
                g_activeCtx, static_cast<uint32_t>(outputFormat),
                renderW, renderH, displayW, displayH);
        }
        if (rc != ADRENA_PLUGIN_OK) {
            AD_LOG_E("DLSS proxy: resize/init failed rc=%d", rc);
            TearDownActive();
            return false;
        }
        g_currentRenderW  = renderW;  g_currentRenderH  = renderH;
        g_currentDisplayW = displayW; g_currentDisplayH = displayH;
        g_historyValid    = false;
    }
    return true;
}

// ───────────────────────── DllMain ─────────────────────────

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID /*lpReserved*/) {
    switch (reason) {
    case DLL_PROCESS_ATTACH:
        DisableThreadLibraryCalls(hModule);
        adrena::Logger::Instance().Init("adrena_proxy.log");
        AD_LOG_I("adrenaproxy_sgsr.dll loaded (AdrenaProxy v2.0 — NVNGX Proxy)");
        {
            adrena::RegisterBuiltinPlugins();
            // Discover any external plugin DLLs shipped next to us.
            adrena::PluginManager::Instance().ScanDefaultDirectory();

            adrena::GPUInfo gpu = adrena::AutoDetectGPU();
            if (auto* ss = adrena::GetSharedState()) {
                adrena::SharedStateLock lock(&ss->lock);
                ss->is_adreno   = gpu.isAdreno;
                ss->adreno_tier = gpu.adrenoTier;
            }
        }
        break;
    case DLL_PROCESS_DETACH:
        AD_LOG_I("adrenaproxy_sgsr.dll unloading");
        TearDownActive();
        adrena::PluginManager::Instance().Shutdown();
        adrena::Logger::Instance().Shutdown();
        break;
    }
    return TRUE;
}

extern "C" {

// ───────────────────────── NVNGX core ─────────────────────────

__declspec(dllexport) int NVNGX_Init(unsigned long long appId, void* device,
                                     void* instance, const void* /*features*/) {
    AD_LOG_I("NVNGX_Init: appId=%llu device=%p instance=%p",
             appId, device, instance);
    if (g_initialized) return NVNGX_SUCCESS;
    if (!device) { AD_LOG_E("NVNGX_Init: null device"); return NVNGX_FAIL; }

    g_device       = static_cast<ID3D12Device*>(device);
    g_initialized  = true;
    g_historyValid = false;

    if (auto* ss = adrena::GetSharedState()) {
        adrena::SharedStateLock lock(&ss->lock);
        ss->sgsr_active  = true;
        ss->sgsr_enabled = true;
    }
    AD_LOG_I("NVNGX_Init ok — proxy ready");
    return NVNGX_SUCCESS;
}

__declspec(dllexport) int NVNGX_Shutdown() {
    AD_LOG_I("NVNGX_Shutdown");
    if (g_initialized) {
        TearDownActive();
        if (g_params) { delete g_params; g_params = nullptr; }
        if (auto* ss = adrena::GetSharedState()) {
            adrena::SharedStateLock l(&ss->lock);
            ss->sgsr_active = false;
            ss->sgsr_enabled = false;
        }
        g_initialized = false;
        g_historyValid = false;
        g_device = nullptr;
    }
    return NVNGX_SUCCESS;
}

__declspec(dllexport) int NVNGX_GetParameters(void** outParams) {
    if (!outParams) return NVNGX_FAIL;
    if (!g_params) g_params = new adrena::NGXParameter();
    *outParams = g_params;
    return NVNGX_SUCCESS;
}

// ───────────────────────── DLSS glue ─────────────────────────

__declspec(dllexport) int DLSS_GetOptimalSettings(
    void* /*nvngxInstance*/,
    unsigned int targetW, unsigned int targetH,
    int perfQuality,
    unsigned int* outRenderW, unsigned int* outRenderH,
    float* outSharpness) {
    AD_LOG_I("DLSS_GetOptimalSettings: target=%ux%u quality=%d",
             targetW, targetH, perfQuality);
    if (!outRenderW || !outRenderH || !outSharpness) return NVNGX_FAIL;

    float scale;
    switch (perfQuality) {
    case 0:  scale = 0.33f; break;
    case 1:  scale = 0.50f; break;
    case 2:  scale = 0.67f; break;
    case 3:  scale = 0.77f; break;
    default: scale = 0.67f; break;
    }
    adrena::Config& cfg = adrena::GetConfig();
    if (cfg.render_scale > 0.0f && cfg.render_scale <= 1.0f) scale = cfg.render_scale;

    *outRenderW   = (unsigned int)(targetW * scale);
    *outRenderH   = (unsigned int)(targetH * scale);
    if (*outRenderW < 1) *outRenderW = 1;
    if (*outRenderH < 1) *outRenderH = 1;
    *outSharpness = cfg.sharpness;

    EnsureActivePlugin(*outRenderW, *outRenderH, targetW, targetH,
                       DXGI_FORMAT_R8G8B8A8_UNORM);

    if (auto* ss = adrena::GetSharedState()) {
        adrena::SharedStateLock lock(&ss->lock);
        ss->render_width   = *outRenderW;
        ss->render_height  = *outRenderH;
        ss->display_width  = targetW;
        ss->display_height = targetH;
        ss->quality_preset = perfQuality;
        ss->sharpness      = *outSharpness;
        ss->render_scale   = scale;
    }
    AD_LOG_I("DLSS_GetOptimalSettings: render=%ux%u scale=%.2f sharp=%.2f",
             *outRenderW, *outRenderH, scale, *outSharpness);
    return NVNGX_SUCCESS;
}

__declspec(dllexport) int DLSS_Evaluate(
    void* /*nvngxInstance*/, void* cmdList, void* params) {
    if (!g_initialized || !cmdList || !params) return NVNGX_FAIL;

    auto* p   = static_cast<adrena::NGXParameter*>(params);
    auto* cl  = static_cast<ID3D12GraphicsCommandList*>(cmdList);

    ID3D12Resource* color = nullptr;
    ID3D12Resource* depth = nullptr;
    ID3D12Resource* motion = nullptr;
    ID3D12Resource* exposure = nullptr;
    ID3D12Resource* output = nullptr;
    float sharpness = 0.80f;
    uint32_t renderW = 0, renderH = 0;
    float jitterX = 0.0f, jitterY = 0.0f;

    for (const char* k : {"Color", "DLSS.Input.Color", "Color.Input"})
        if (p->GetValueD3D12Res(k, &color) == 0 && color) break;
    for (const char* k : {"Depth", "DLSS.Input.Depth", "Depth.Input"})
        if (p->GetValueD3D12Res(k, &depth) == 0 && depth) break;
    for (const char* k : {"MotionVectors", "DLSS.Input.MotionVectors", "MV.Input"})
        if (p->GetValueD3D12Res(k, &motion) == 0 && motion) break;
    for (const char* k : {"ExposureTexture", "DLSS.Input.Exposure"})
        if (p->GetValueD3D12Res(k, &exposure) == 0 && exposure) break;
    for (const char* k : {"Output", "DLSS.Output", "Output.Backbuffer"})
        if (p->GetValueD3D12Res(k, &output) == 0 && output) break;

    p->GetValueF32("Sharpness", &sharpness);
    p->GetValueUI32("Render.Subrect.Width",  &renderW);
    p->GetValueUI32("Render.Subrect.Height", &renderH);
    p->GetValueF32("Jitter.Offset.X", &jitterX);
    p->GetValueF32("Jitter.Offset.Y", &jitterY);

    uint32_t displayW = renderW, displayH = renderH;
    if (auto* ss = adrena::GetSharedState()) {
        adrena::SharedStateLock lock(&ss->lock);
        if (renderW == 0) renderW = ss->render_width;
        if (renderH == 0) renderH = ss->render_height;
        displayW  = ss->display_width;
        displayH  = ss->display_height;
        sharpness = ss->sharpness;
    }

    if (!output || !color) return NVNGX_SUCCESS;

    if (!EnsureActivePlugin(renderW, renderH, displayW, displayH,
                            DXGI_FORMAT_R8G8B8A8_UNORM)) {
        return NVNGX_SUCCESS;  // nothing to do (upscaling disabled or failed)
    }

    AdrenaUpscaleParams up{};
    up.abi_version           = ADRENA_PLUGIN_ABI_VERSION;
    up.api                   = ADRENA_GFX_D3D12;
    up.cmd_list              = cl;
    up.input_color           = color;
    up.input_depth           = depth;
    up.input_motion          = motion;
    up.input_exposure        = exposure;
    up.output                = output;
    up.render_w              = renderW;
    up.render_h              = renderH;
    up.display_w             = displayW ? displayW : renderW;
    up.display_h             = displayH ? displayH : renderH;
    up.sharpness             = sharpness;
    up.jitter_x              = jitterX;
    up.jitter_y              = jitterY;
    up.delta_time_seconds    = 0.0f;
    up.pre_exposure          = 1.0f;
    up.camera_near           = 0.01f;
    up.camera_far            = 1000.0f;
    up.camera_fov_h_radians  = 1.0472f;
    up.min_lerp_contribution = 0.15f;
    up.motion_vector_scale   = 1;
    up.reset_history         = g_historyValid ? 0u : 1u;
    up.same_camera           = 1u;
    // clip_to_prev_clip left zero-initialized (no matrix available here).

    const int rc = g_activePlugin->upscaler_vtable->execute(g_activeCtx, &up);
    if (rc != ADRENA_PLUGIN_OK) {
        AD_LOG_W("DLSS_Evaluate: plugin '%s' execute -> %d",
                 g_activeName.c_str(), rc);
        return NVNGX_FAIL;
    }
    g_historyValid = true;
    return NVNGX_SUCCESS;
}

// ───────────────────────── NVSDK_NGX parameter accessors ─────────────────────────

__declspec(dllexport) int NVSDK_NGX_Parameter_SetValue_UI32(void* params, const char* k, uint32_t v) {
    return params ? static_cast<adrena::NGXParameter*>(params)->SetValueUI32(k, v) : -1;
}
__declspec(dllexport) int NVSDK_NGX_Parameter_SetValue_I32(void* params, const char* k, int32_t v) {
    return params ? static_cast<adrena::NGXParameter*>(params)->SetValueI32(k, v) : -1;
}
__declspec(dllexport) int NVSDK_NGX_Parameter_SetValue_F32(void* params, const char* k, float v) {
    return params ? static_cast<adrena::NGXParameter*>(params)->SetValueF32(k, v) : -1;
}
__declspec(dllexport) int NVSDK_NGX_Parameter_SetValue_F64(void* params, const char* k, double v) {
    return params ? static_cast<adrena::NGXParameter*>(params)->SetValueF64(k, v) : -1;
}
__declspec(dllexport) int NVSDK_NGX_Parameter_SetValue_UI64(void* params, const char* k, uint64_t v) {
    return params ? static_cast<adrena::NGXParameter*>(params)->SetValueUI64(k, v) : -1;
}
__declspec(dllexport) int NVSDK_NGX_Parameter_SetValue_Ptr(void* params, const char* k, void* v) {
    return params ? static_cast<adrena::NGXParameter*>(params)->SetValuePtr(k, v) : -1;
}
__declspec(dllexport) int NVSDK_NGX_Parameter_SetValue_D3d12Resource(void* params, const char* k, ID3D12Resource* v) {
    return params ? static_cast<adrena::NGXParameter*>(params)->SetValueD3D12Res(k, v) : -1;
}
__declspec(dllexport) int NVSDK_NGX_Parameter_GetValue_UI32(void* params, const char* k, uint32_t* v) {
    return params ? static_cast<adrena::NGXParameter*>(params)->GetValueUI32(k, v) : -1;
}
__declspec(dllexport) int NVSDK_NGX_Parameter_GetValue_I32(void* params, const char* k, int32_t* v) {
    return params ? static_cast<adrena::NGXParameter*>(params)->GetValueI32(k, v) : -1;
}
__declspec(dllexport) int NVSDK_NGX_Parameter_GetValue_F32(void* params, const char* k, float* v) {
    return params ? static_cast<adrena::NGXParameter*>(params)->GetValueF32(k, v) : -1;
}
__declspec(dllexport) int NVSDK_NGX_Parameter_GetValue_F64(void* params, const char* k, double* v) {
    return params ? static_cast<adrena::NGXParameter*>(params)->GetValueF64(k, v) : -1;
}
__declspec(dllexport) int NVSDK_NGX_Parameter_GetValue_UI64(void* params, const char* k, uint64_t* v) {
    return params ? static_cast<adrena::NGXParameter*>(params)->GetValueUI64(k, v) : -1;
}
__declspec(dllexport) int NVSDK_NGX_Parameter_GetValue_Ptr(void* params, const char* k, void** v) {
    return params ? static_cast<adrena::NGXParameter*>(params)->GetValuePtr(k, v) : -1;
}
__declspec(dllexport) int NVSDK_NGX_Parameter_GetValue_D3d12Resource(void* params, const char* k, ID3D12Resource** v) {
    return params ? static_cast<adrena::NGXParameter*>(params)->GetValueD3D12Res(k, v) : -1;
}
__declspec(dllexport) void NVSDK_NGX_Parameter_Reset(void* params) {
    if (params) static_cast<adrena::NGXParameter*>(params)->Reset();
}
__declspec(dllexport) void NVSDK_NGX_Parameter_Destroy(void* params) {
    if (params) delete static_cast<adrena::NGXParameter*>(params);
}

} // extern "C"
