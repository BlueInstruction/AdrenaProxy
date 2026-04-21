#include "ngx_param.h"
#include <adrena_core/config.h>
#include <adrena_core/logger.h>
#include <adrena_core/gpu_detect.h>
#include <adrena_core/shared_state.h>
#include <adrena_core/plugin_manager.h>

#include <windows.h>
#include <d3d11.h>
#include <d3d12.h>
#include <cstdint>
#include <cstring>
#include <mutex>

// ──────────────────────────────────────────────────────────────────
//  NGX result codes — match NVIDIA's public <nvsdk_ngx_defs.h>.
// ──────────────────────────────────────────────────────────────────
static constexpr int NVNGX_SUCCESS = 0x1;
static constexpr int NVNGX_FAIL    = 0x0;

// NVSDK_NGX_Result values (subset we need).  These are the exact bit
// patterns NVIDIA uses; DLSS games compare return codes against them
// via NVSDK_NGX_SUCCEED / NVSDK_NGX_FAILED macros.
static constexpr int NVSDK_NGX_Result_Success                 = 0x1;
static constexpr int NVSDK_NGX_Result_Fail                    = static_cast<int>(0xBAD00000);
static constexpr int NVSDK_NGX_Result_FAIL_FeatureNotSupported= static_cast<int>(0xBAD00002);
static constexpr int NVSDK_NGX_Result_FAIL_FeatureNotFound    = static_cast<int>(0xBAD00005);
static constexpr int NVSDK_NGX_Result_FAIL_InvalidParameter   = static_cast<int>(0xBAD00006);

// NVSDK_NGX_Feature enum values (subset).
static constexpr int NVSDK_NGX_Feature_SuperSampling          = 1;   // DLSS
static constexpr int NVSDK_NGX_Feature_InPainting             = 2;
static constexpr int NVSDK_NGX_Feature_ImageSuperResolution   = 3;
static constexpr int NVSDK_NGX_Feature_SlowMotion             = 4;
static constexpr int NVSDK_NGX_Feature_VideoSuperResolution   = 5;
static constexpr int NVSDK_NGX_Feature_Reserved1              = 6;
static constexpr int NVSDK_NGX_Feature_Reserved2              = 7;
static constexpr int NVSDK_NGX_Feature_Reserved3              = 8;
static constexpr int NVSDK_NGX_Feature_FrameGeneration        = 11;  // DLSS-G
static constexpr int NVSDK_NGX_Feature_DeepResolve            = 12;
static constexpr int NVSDK_NGX_Feature_RayReconstruction      = 13;

// ── Process-wide mutex guarding all proxy globals ──
static std::mutex g_proxyMutex;

static bool                 g_initialized  = false;
static void*                g_device       = nullptr;
static adrena::NGXParameter* g_params      = nullptr;
static bool                 g_historyValid = false;

// Active upscaler id (selected from Config::sgsr_mode; kept in sync with
// the plugin actually initialised on the device).
static char g_activeId[32] = "sgsr1";

// Opaque NGX handles returned from CreateFeature — game keeps these and
// passes them back into EvaluateFeature / ReleaseFeature.  Our handles
// are just tagged integers; we don't track per-feature state.
struct NVSDK_NGX_Handle { uint32_t Id; uint32_t Feature; };

// Track last dimensions to avoid unnecessary Resize() calls that destroy
// GPU resources and reset temporal history on every GetOptimalSettings.
static unsigned int g_lastRenderW  = 0;
static unsigned int g_lastRenderH  = 0;
static unsigned int g_lastDisplayW = 0;
static unsigned int g_lastDisplayH = 0;

// Deferred one-shot flag — GPU detection + shared state init happens on
// the first NVNGX_Init call instead of DllMain (avoids loader-lock hazard).
static bool g_earlyInitDone = false;

// ──────────────────────────────────────────────────────────────────
//  Helpers
// ──────────────────────────────────────────────────────────────────
static void GetPluginDir(wchar_t* out, DWORD outLen) {
    wchar_t self[MAX_PATH]{};
    GetModuleFileNameW(
        GetModuleHandleW(L"adrenaproxy_sgsr.dll"), self, MAX_PATH);
    wchar_t* last = wcsrchr(self, L'\\');
    if (last) *last = L'\0';
    swprintf(out, outLen, L"%s\\plugins", self);
}

static uint32_t PluginIdToTelemetryCode(const char* id) {
    if (!id) return 0;
    if (!std::strcmp(id, "sgsr1")) return 1;
    if (!std::strcmp(id, "sgsr2")) return 2;
    if (!std::strcmp(id, "fsr2"))  return 3;
    if (!std::strcmp(id, "xess"))  return 4;
    if (!std::strcmp(id, "fsr3"))  return 5;
    if (!std::strcmp(id, "fsr4"))  return 6;
    return 9;
}

// DLSS quality preset → render scale, matching NVIDIA's
// NVSDK_NGX_PerfQuality_Value convention exactly.  NOTE:
// UltraPerformance is value 3 and UltraQuality is value 4 — the
// previous code had these swapped, which made selecting
// "Ultra Performance" in the game actually give a Quality-grade 0.77x
// render scale and made UltraQuality unreachable.
static float QualityPresetToScale(int perfQuality) {
    switch (perfQuality) {
    case 0:  return 0.50f;  // MaxPerf           → Performance
    case 1:  return 0.58f;  // Balanced
    case 2:  return 0.67f;  // MaxQuality        → Quality
    case 3:  return 0.33f;  // UltraPerformance
    case 4:  return 0.77f;  // UltraQuality
    case 5:  return 1.00f;  // DLAA
    default: return 0.67f;
    }
}

static const char* QualityPresetName(int perfQuality) {
    switch (perfQuality) {
    case 0:  return "Performance";
    case 1:  return "Balanced";
    case 2:  return "Quality";
    case 3:  return "UltraPerformance";
    case 4:  return "UltraQuality";
    case 5:  return "DLAA";
    default: return "Quality(fallback)";
    }
}

static adrena::IPlugin* ResolveActivePlugin() {
    adrena::Config& cfg = adrena::GetConfig();
    const char* wanted = "sgsr1";
    switch (cfg.sgsr_mode) {
    case adrena::SGSRMode::SGSR2: wanted = "sgsr2"; break;
    case adrena::SGSRMode::SGSR1:
    default:                      wanted = "sgsr1"; break;
    }
    std::strncpy(g_activeId, wanted, sizeof(g_activeId) - 1);
    g_activeId[sizeof(g_activeId) - 1] = '\0';
    return adrena::PluginManager::Get().Find(wanted);
}

// ──────────────────────────────────────────────────────────────────
//  Deferred early init — called from NVNGX_Init, NOT from DllMain.
// ──────────────────────────────────────────────────────────────────
static void EnsureEarlyInit() {
    if (g_earlyInitDone) return;
    g_earlyInitDone = true;

    adrena::Logger::Instance().Init("adrena_proxy.log");
    AD_LOG_I("adrenaproxy_sgsr.dll loaded (AdrenaProxy v2.0 — NVNGX Proxy)");

    adrena::GPUInfo gpu = adrena::AutoDetectGPU();
    adrena::SharedState* ss = adrena::GetSharedState();
    if (ss) {
        adrena::SharedStateLock lock(&ss->lock);
        ss->is_adreno   = gpu.isAdreno;
        ss->adreno_tier = gpu.adrenoTier;
    }

    adrena::PluginManager::Get().RegisterBuiltins();
    wchar_t pluginDir[MAX_PATH];
    GetPluginDir(pluginDir, MAX_PATH);
    adrena::PluginManager::Get().LoadExternalPlugins(pluginDir);
}

// ──────────────────────────────────────────────────────────────────
//  DllMain
// ──────────────────────────────────────────────────────────────────
BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID /*lpReserved*/) {
    switch (reason) {
    case DLL_PROCESS_ATTACH:
        DisableThreadLibraryCalls(hModule);
        break;
    case DLL_PROCESS_DETACH:
        adrena::Logger::Instance().Shutdown();
        break;
    }
    return TRUE;
}

// ──────────────────────────────────────────────────────────────────
//  Core implementation helpers (reused by both NVNGX_* and NVSDK_NGX_*
//  entry points).  All require g_proxyMutex to be held by the caller.
// ──────────────────────────────────────────────────────────────────
static int DoInit(void* device) {
    EnsureEarlyInit();
    if (g_initialized) return NVNGX_SUCCESS;
    if (!device) { AD_LOG_E("NGX Init: null device"); return NVNGX_FAIL; }

    g_device        = device;
    g_initialized   = true;
    g_historyValid  = false;

    if (adrena::SharedState* ss = adrena::GetSharedState()) {
        adrena::SharedStateLock lock(&ss->lock);
        ss->sgsr_active  = true;
        ss->sgsr_enabled = true;
        ++ss->dlss_init_count;
    }
    AD_LOG_I("NGX Init succeeded — SGSR will handle DLSS calls");
    return NVNGX_SUCCESS;
}

static int DoShutdown() {
    AD_LOG_I("NGX Shutdown called");
    if (g_initialized) {
        adrena::PluginManager::Get().UnloadAll();
        if (g_params) { delete g_params; g_params = nullptr; }
        if (adrena::SharedState* ss = adrena::GetSharedState()) {
            adrena::SharedStateLock l(&ss->lock);
            ss->sgsr_active  = false;
            ss->sgsr_enabled = false;
        }
        g_initialized   = false;
        g_historyValid  = false;
        g_device        = nullptr;
        g_lastRenderW   = g_lastRenderH = g_lastDisplayW = g_lastDisplayH = 0;
    }
    return NVNGX_SUCCESS;
}

// Create/allocate an NGX parameter block pre-populated with capability
// flags so games' feasibility checks succeed.  This is what makes
// "NVSDK_NGX_Parameter_SuperSampling_Available" query come back as 1
// and convinces the game to actually take the DLSS code path.
static adrena::NGXParameter* AllocateCapabilityParams() {
    auto* p = new adrena::NGXParameter();
    p->SetValueI32 ("SuperSampling.Available",                   1);
    p->SetValueI32 ("NVSDK_NGX_Parameter_SuperSampling_Available",1);
    p->SetValueI32 ("FrameInterpolation.Available",              1);
    p->SetValueI32 ("NVSDK_NGX_Parameter_FrameInterpolation_Available", 1);
    p->SetValueI32 ("SuperSampling.FeatureInitResult",           0);
    p->SetValueUI32("SuperSampling.NeedsUpdatedDriver",          0);
    p->SetValueUI32("SuperSampling.MinDriverVersionMajor",       0);
    p->SetValueUI32("SuperSampling.MinDriverVersionMinor",       0);
    p->SetValueUI32("NVSDK_NGX_Parameter_SuperSampling_FeatureInitResult", 0);
    p->SetValueUI32("NVSDK_NGX_Parameter_SuperSampling_NeedsUpdatedDriver", 0);
    return p;
}

extern "C" {

// ══════════════════════════════════════════════════════════════════
//  Legacy NVNGX_* exports (kept for back-compat with earlier proxies)
// ══════════════════════════════════════════════════════════════════

__declspec(dllexport) int NVNGX_Init(unsigned long long appId, void* device,
                                     void* instance, const void* /*features*/) {
    std::lock_guard<std::mutex> guard(g_proxyMutex);
    AD_LOG_I("NVNGX_Init: appId=%llu device=%p instance=%p", appId, device, instance);
    return DoInit(device);
}

__declspec(dllexport) int NVNGX_Shutdown() {
    std::lock_guard<std::mutex> guard(g_proxyMutex);
    return DoShutdown();
}

__declspec(dllexport) int NVNGX_GetParameters(void** outParams) {
    std::lock_guard<std::mutex> guard(g_proxyMutex);
    AD_LOG_I("NVNGX_GetParameters called");
    if (!outParams) return NVNGX_FAIL;
    if (!g_params) g_params = AllocateCapabilityParams();
    *outParams = g_params;
    return NVNGX_SUCCESS;
}

__declspec(dllexport) int DLSS_GetOptimalSettings(
    void* /*nvngxInstance*/,
    unsigned int targetW, unsigned int targetH,
    int perfQuality,
    unsigned int* outRenderW, unsigned int* outRenderH,
    float* outSharpness)
{
    std::lock_guard<std::mutex> guard(g_proxyMutex);
    AD_LOG_I("DLSS_GetOptimalSettings: target=%ux%u quality=%d (%s)",
             targetW, targetH, perfQuality, QualityPresetName(perfQuality));

    if (!outRenderW || !outRenderH || !outSharpness) return NVNGX_FAIL;

    // 1. Start from the game-requested preset (now uses the correct
    //    NVIDIA enum mapping — Ultra Performance is 3, not 0).
    float scale = QualityPresetToScale(perfQuality);

    // 2. Honour an explicit user override from INI only when it's set
    //    to a sensible value.  Previously `cfg.render_scale` (which
    //    always has a derived default) was used as the override source
    //    which silently nullified the game's quality choice.  Now only
    //    `custom_scale` — explicitly opted in by the user — wins.
    adrena::Config& cfg = adrena::GetConfig();
    if (cfg.custom_scale > 0.0f && cfg.custom_scale <= 1.0f) {
        scale = cfg.custom_scale;
        AD_LOG_I("DLSS_GetOptimalSettings: overriding with custom_scale=%.2f", scale);
    }

    *outRenderW = (unsigned int)(targetW * scale);
    *outRenderH = (unsigned int)(targetH * scale);
    if (*outRenderW < 1) *outRenderW = 1;
    if (*outRenderH < 1) *outRenderH = 1;
    *outSharpness = cfg.sharpness;

    // 3. Init/resize the active upscaler if dimensions changed.
    bool dimsChanged = (*outRenderW != g_lastRenderW  || *outRenderH != g_lastRenderH ||
                        targetW     != g_lastDisplayW || targetH     != g_lastDisplayH);

    if (g_device) {
        ID3D12Device* d3dDevice = static_cast<ID3D12Device*>(g_device);
        adrena::IPlugin* plugin = ResolveActivePlugin();
        if (plugin) {
            if (!plugin->IsInitialized()) {
                if (!plugin->Init(d3dDevice, DXGI_FORMAT_R8G8B8A8_UNORM,
                                  *outRenderW, *outRenderH, targetW, targetH)) {
                    AD_LOG_E("DLSS_GetOptimalSettings: %s Init failed", g_activeId);
                    return NVNGX_FAIL;
                }
                g_historyValid = false;
            } else if (dimsChanged) {
                if (!plugin->Resize(*outRenderW, *outRenderH, targetW, targetH)) {
                    AD_LOG_E("DLSS_GetOptimalSettings: %s Resize failed", g_activeId);
                    return NVNGX_FAIL;
                }
                g_historyValid = false;
            }
        } else {
            AD_LOG_W("DLSS_GetOptimalSettings: no plugin registered for id=%s", g_activeId);
        }
    }

    g_lastRenderW  = *outRenderW;
    g_lastRenderH  = *outRenderH;
    g_lastDisplayW = targetW;
    g_lastDisplayH = targetH;

    if (adrena::SharedState* ss = adrena::GetSharedState()) {
        adrena::SharedStateLock lock(&ss->lock);
        ss->render_width   = *outRenderW;
        ss->render_height  = *outRenderH;
        ss->display_width  = targetW;
        ss->display_height = targetH;
        ss->quality_preset = perfQuality;
        ss->sharpness      = *outSharpness;
        ss->render_scale   = scale;
    }

    AD_LOG_I("DLSS_GetOptimalSettings: render=%ux%u scale=%.2f sharp=%.2f preset=%s",
             *outRenderW, *outRenderH, scale, *outSharpness,
             QualityPresetName(perfQuality));
    return NVNGX_SUCCESS;
}

__declspec(dllexport) int DLSS_Evaluate(
    void* /*nvngxInstance*/,
    void* cmdList,
    void* params)
{
    std::lock_guard<std::mutex> guard(g_proxyMutex);
    if (!g_initialized || !cmdList || !params) return NVNGX_FAIL;

    adrena::NGXParameter* p = static_cast<adrena::NGXParameter*>(params);
    ID3D12GraphicsCommandList* cl = static_cast<ID3D12GraphicsCommandList*>(cmdList);

    // Expanded key-name dictionaries.  Different engines use different
    // spellings; a single canonical list here catches every shipping
    // DLSS-enabled title we're aware of.
    static const char* kColorKeys[] = {
        "Color", "Color.Input", "DLSS.Input.Color",
        "NVSDK_NGX_Parameter_Color",
        "DLSS_Color", "InputColor",
    };
    static const char* kDepthKeys[] = {
        "Depth", "Depth.Input", "DLSS.Input.Depth",
        "NVSDK_NGX_Parameter_Depth",
        "DLSS_Depth", "InputDepth",
    };
    static const char* kMotionKeys[] = {
        "MotionVectors", "MV.Input", "DLSS.Input.MotionVectors",
        "NVSDK_NGX_Parameter_MotionVectors",
        "DLSS_MotionVectors", "InputMotionVectors",
    };
    static const char* kOutputKeys[] = {
        "Output", "Output.Backbuffer", "DLSS.Output",
        "NVSDK_NGX_Parameter_Output",
        "DLSS_Output", "OutputColor", "FinalOutput",
    };

    ID3D12Resource* color  = p->FindD3D12ByAny(kColorKeys , (int)(sizeof(kColorKeys )/sizeof(*kColorKeys )));
    ID3D12Resource* depth  = p->FindD3D12ByAny(kDepthKeys , (int)(sizeof(kDepthKeys )/sizeof(*kDepthKeys )));
    ID3D12Resource* motion = p->FindD3D12ByAny(kMotionKeys, (int)(sizeof(kMotionKeys)/sizeof(*kMotionKeys)));
    ID3D12Resource* output = p->FindD3D12ByAny(kOutputKeys, (int)(sizeof(kOutputKeys)/sizeof(*kOutputKeys)));

    float    sharpness = 0.80f;
    uint32_t renderW = 0, renderH = 0;
    p->GetValueF32 ("Sharpness",              &sharpness);
    p->GetValueUI32("Render.Subrect.Width",   &renderW);
    p->GetValueUI32("Render.Subrect.Height",  &renderH);
    if (renderW == 0) p->GetValueUI32("DLSS.Render.Subrect.Width",  &renderW);
    if (renderH == 0) p->GetValueUI32("DLSS.Render.Subrect.Height", &renderH);

    uint32_t displayW = 0;
    uint32_t displayH = 0;
    if (adrena::SharedState* ss = adrena::GetSharedState()) {
        adrena::SharedStateLock lock(&ss->lock);
        if (renderW == 0) renderW = ss->render_width;
        if (renderH == 0) renderH = ss->render_height;
        displayW = ss->display_width;
        displayH = ss->display_height;
        sharpness = ss->sharpness;
    }
    if (displayW == 0) displayW = renderW;
    if (displayH == 0) displayH = renderH;

    if (renderW == 0 || renderH == 0) {
        AD_LOG_E("DLSS_Evaluate: zero render dimensions (%ux%u), skipping", renderW, renderH);
        return NVNGX_FAIL;
    }

    AD_LOG_I("DLSS_Evaluate: color=%p depth=%p motion=%p output=%p render=%ux%u sharp=%.2f",
             color, depth, motion, output, renderW, renderH, sharpness);

    adrena::IPlugin* plugin = ResolveActivePlugin();
    uint32_t plugin_id_code = PluginIdToTelemetryCode(g_activeId);
    bool ok = false;

    if (plugin && plugin->IsInitialized() && output && color) {
        AdrenaUpscaleParams up{};
        up.color               = color;
        up.depth               = depth;
        up.motion              = motion;
        up.output              = output;
        up.sharpness           = sharpness;
        up.renderWidth         = renderW;
        up.renderHeight        = renderH;
        up.displayWidth        = displayW ? displayW : renderW;
        up.displayHeight       = displayH ? displayH : renderH;
        up.resetHistory        = g_historyValid ? 0 : 1;
        up.preExposure         = 1.0f;
        up.minLerpContribution = 0.15f;
        up.sameCamera          = 1;
        up.cameraFovH          = 1.0472f;
        up.cameraNear          = 0.01f;
        up.cameraFar           = 1000.0f;

        plugin->Execute(cl, up);
        g_historyValid = true;
        ok = true;
        AD_LOG_I("DLSS_Evaluate: %s executed", g_activeId);
    } else if (!plugin) {
        AD_LOG_W("DLSS_Evaluate: no plugin for id=%s", g_activeId);
    } else if (!plugin->IsInitialized()) {
        AD_LOG_W("DLSS_Evaluate: plugin %s not initialised", g_activeId);
    } else if (!output || !color) {
        AD_LOG_W("DLSS_Evaluate: missing resources (color=%p output=%p) — game probably uses "
                 "a DLSS parameter key name we don't recognise yet",
                 color, output);
    }

    if (adrena::SharedState* ss = adrena::GetSharedState()) {
        adrena::SharedStateLock lock(&ss->lock);
        ++ss->dlss_evaluate_count;
        ss->dlss_last_plugin_ok = ok ? 1u : 0u;
        ss->dlss_last_plugin_id = plugin_id_code;
    }

    return NVNGX_SUCCESS;
}

// ══════════════════════════════════════════════════════════════════
//  C-style parameter accessor exports (legacy; kept for games that
//  link against the pre-public NGX "C API").
// ══════════════════════════════════════════════════════════════════

__declspec(dllexport) int NVSDK_NGX_Parameter_SetValue_UI32(void* p, const char* k, uint32_t v) {
    return p ? static_cast<adrena::NGXParameter*>(p)->SetValueUI32(k, v) : -1;
}
__declspec(dllexport) int NVSDK_NGX_Parameter_SetValue_I32(void* p, const char* k, int32_t v) {
    return p ? static_cast<adrena::NGXParameter*>(p)->SetValueI32(k, v) : -1;
}
__declspec(dllexport) int NVSDK_NGX_Parameter_SetValue_F32(void* p, const char* k, float v) {
    return p ? static_cast<adrena::NGXParameter*>(p)->SetValueF32(k, v) : -1;
}
__declspec(dllexport) int NVSDK_NGX_Parameter_SetValue_F64(void* p, const char* k, double v) {
    return p ? static_cast<adrena::NGXParameter*>(p)->SetValueF64(k, v) : -1;
}
__declspec(dllexport) int NVSDK_NGX_Parameter_SetValue_UI64(void* p, const char* k, uint64_t v) {
    return p ? static_cast<adrena::NGXParameter*>(p)->SetValueUI64(k, v) : -1;
}
__declspec(dllexport) int NVSDK_NGX_Parameter_SetValue_Ptr(void* p, const char* k, void* v) {
    return p ? static_cast<adrena::NGXParameter*>(p)->SetValuePtr(k, v) : -1;
}
__declspec(dllexport) int NVSDK_NGX_Parameter_SetValue_D3d11Resource(void* p, const char* k, ID3D11Resource* v) {
    return p ? static_cast<adrena::NGXParameter*>(p)->SetValueD3D11Res(k, v) : -1;
}
__declspec(dllexport) int NVSDK_NGX_Parameter_SetValue_D3d12Resource(void* p, const char* k, ID3D12Resource* v) {
    return p ? static_cast<adrena::NGXParameter*>(p)->SetValueD3D12Res(k, v) : -1;
}

__declspec(dllexport) int NVSDK_NGX_Parameter_GetValue_UI32(void* p, const char* k, uint32_t* v) {
    return p ? static_cast<adrena::NGXParameter*>(p)->GetValueUI32(k, v) : -1;
}
__declspec(dllexport) int NVSDK_NGX_Parameter_GetValue_I32(void* p, const char* k, int32_t* v) {
    return p ? static_cast<adrena::NGXParameter*>(p)->GetValueI32(k, v) : -1;
}
__declspec(dllexport) int NVSDK_NGX_Parameter_GetValue_F32(void* p, const char* k, float* v) {
    return p ? static_cast<adrena::NGXParameter*>(p)->GetValueF32(k, v) : -1;
}
__declspec(dllexport) int NVSDK_NGX_Parameter_GetValue_F64(void* p, const char* k, double* v) {
    return p ? static_cast<adrena::NGXParameter*>(p)->GetValueF64(k, v) : -1;
}
__declspec(dllexport) int NVSDK_NGX_Parameter_GetValue_UI64(void* p, const char* k, uint64_t* v) {
    return p ? static_cast<adrena::NGXParameter*>(p)->GetValueUI64(k, v) : -1;
}
__declspec(dllexport) int NVSDK_NGX_Parameter_GetValue_Ptr(void* p, const char* k, void** v) {
    return p ? static_cast<adrena::NGXParameter*>(p)->GetValuePtr(k, v) : -1;
}
__declspec(dllexport) int NVSDK_NGX_Parameter_GetValue_D3d11Resource(void* p, const char* k, ID3D11Resource** v) {
    return p ? static_cast<adrena::NGXParameter*>(p)->GetValueD3D11Res(k, v) : -1;
}
__declspec(dllexport) int NVSDK_NGX_Parameter_GetValue_D3d12Resource(void* p, const char* k, ID3D12Resource** v) {
    return p ? static_cast<adrena::NGXParameter*>(p)->GetValueD3D12Res(k, v) : -1;
}

__declspec(dllexport) void NVSDK_NGX_Parameter_Reset(void* p) {
    if (p) static_cast<adrena::NGXParameter*>(p)->Reset();
}
__declspec(dllexport) void NVSDK_NGX_Parameter_Destroy(void* p) {
    if (p) {
        if (p == g_params) g_params = nullptr;
        delete static_cast<adrena::NGXParameter*>(p);
    }
}

// ══════════════════════════════════════════════════════════════════
//  Public NVSDK_NGX_D3D12_* exports — what real DLSS games actually
//  import.  Most games link against NGX Helpers which calls these
//  symbols directly.  Missing any of them is a common "DLSS does
//  nothing in-game" failure mode.
// ══════════════════════════════════════════════════════════════════

__declspec(dllexport) int NVSDK_NGX_D3D12_Init(
    unsigned long long appId,
    const wchar_t*     /*appDataPath*/,
    void*              device,
    const void*        /*featureInfo*/,
    int                /*sdkVersion*/)
{
    std::lock_guard<std::mutex> guard(g_proxyMutex);
    AD_LOG_I("NVSDK_NGX_D3D12_Init: appId=%llu device=%p", appId, device);
    return DoInit(device) == NVNGX_SUCCESS
        ? NVSDK_NGX_Result_Success
        : NVSDK_NGX_Result_Fail;
}

__declspec(dllexport) int NVSDK_NGX_D3D12_Init_Ext(
    unsigned long long appId,
    const wchar_t*     /*appDataPath*/,
    void*              device,
    int                /*featureInfoVersion*/,
    const void*        /*featureInfo*/)
{
    std::lock_guard<std::mutex> guard(g_proxyMutex);
    AD_LOG_I("NVSDK_NGX_D3D12_Init_Ext: appId=%llu device=%p", appId, device);
    return DoInit(device) == NVNGX_SUCCESS
        ? NVSDK_NGX_Result_Success
        : NVSDK_NGX_Result_Fail;
}

__declspec(dllexport) int NVSDK_NGX_D3D12_Init_ProjectID(
    const char*        /*projectId*/,
    int                /*engineType*/,
    const char*        /*engineVersion*/,
    const wchar_t*     /*appDataPath*/,
    void*              device,
    int                /*featureInfoVersion*/,
    const void*        /*featureInfo*/)
{
    std::lock_guard<std::mutex> guard(g_proxyMutex);
    AD_LOG_I("NVSDK_NGX_D3D12_Init_ProjectID: device=%p", device);
    return DoInit(device) == NVNGX_SUCCESS
        ? NVSDK_NGX_Result_Success
        : NVSDK_NGX_Result_Fail;
}

__declspec(dllexport) int NVSDK_NGX_D3D12_Shutdown(void) {
    std::lock_guard<std::mutex> guard(g_proxyMutex);
    DoShutdown();
    return NVSDK_NGX_Result_Success;
}

__declspec(dllexport) int NVSDK_NGX_D3D12_Shutdown1(void* /*device*/) {
    return NVSDK_NGX_D3D12_Shutdown();
}

__declspec(dllexport) int NVSDK_NGX_D3D12_GetParameters(adrena::NVSDK_NGX_Parameter** outParams) {
    std::lock_guard<std::mutex> guard(g_proxyMutex);
    if (!outParams) return NVSDK_NGX_Result_FAIL_InvalidParameter;
    if (!g_params) g_params = AllocateCapabilityParams();
    *outParams = g_params;
    AD_LOG_I("NVSDK_NGX_D3D12_GetParameters -> %p", *outParams);
    return NVSDK_NGX_Result_Success;
}

__declspec(dllexport) int NVSDK_NGX_D3D12_GetCapabilityParameters(adrena::NVSDK_NGX_Parameter** outParams) {
    std::lock_guard<std::mutex> guard(g_proxyMutex);
    if (!outParams) return NVSDK_NGX_Result_FAIL_InvalidParameter;
    // Always hand back a fresh block for capability queries — some games
    // call Reset() on this one and we don't want that to wipe the shared
    // per-frame param block used by Evaluate.
    *outParams = AllocateCapabilityParams();
    AD_LOG_I("NVSDK_NGX_D3D12_GetCapabilityParameters -> %p", *outParams);
    return NVSDK_NGX_Result_Success;
}

__declspec(dllexport) int NVSDK_NGX_D3D12_AllocateParameters(adrena::NVSDK_NGX_Parameter** outParams) {
    if (!outParams) return NVSDK_NGX_Result_FAIL_InvalidParameter;
    *outParams = AllocateCapabilityParams();
    return NVSDK_NGX_Result_Success;
}

__declspec(dllexport) int NVSDK_NGX_D3D12_DestroyParameters(adrena::NVSDK_NGX_Parameter* params) {
    std::lock_guard<std::mutex> guard(g_proxyMutex);
    if (!params) return NVSDK_NGX_Result_FAIL_InvalidParameter;
    if (params == g_params) g_params = nullptr;
    delete static_cast<adrena::NGXParameter*>(params);
    return NVSDK_NGX_Result_Success;
}

__declspec(dllexport) int NVSDK_NGX_D3D12_CreateFeature(
    void*                          cmdList,
    int                            featureId,
    adrena::NVSDK_NGX_Parameter*   /*inParams*/,
    NVSDK_NGX_Handle**             outHandle)
{
    std::lock_guard<std::mutex> guard(g_proxyMutex);
    if (!outHandle) return NVSDK_NGX_Result_FAIL_InvalidParameter;

    auto* h = new NVSDK_NGX_Handle{};
    static uint32_t s_nextId = 0x5050F000u;
    h->Id      = ++s_nextId;
    h->Feature = (uint32_t)featureId;
    *outHandle = h;

    AD_LOG_I("NVSDK_NGX_D3D12_CreateFeature: featureId=%d handle=%u cmdList=%p",
             featureId, h->Id, cmdList);

    // Reject anything we don't currently handle rather than silently
    // returning success — the game will log this and fall back cleanly.
    if (featureId != NVSDK_NGX_Feature_SuperSampling   &&
        featureId != NVSDK_NGX_Feature_FrameGeneration &&
        featureId != NVSDK_NGX_Feature_RayReconstruction) {
        AD_LOG_W("NVSDK_NGX_D3D12_CreateFeature: unsupported featureId=%d", featureId);
        return NVSDK_NGX_Result_FAIL_FeatureNotSupported;
    }

    return NVSDK_NGX_Result_Success;
}

__declspec(dllexport) int NVSDK_NGX_D3D12_ReleaseFeature(NVSDK_NGX_Handle* handle) {
    std::lock_guard<std::mutex> guard(g_proxyMutex);
    if (!handle) return NVSDK_NGX_Result_FAIL_InvalidParameter;
    AD_LOG_I("NVSDK_NGX_D3D12_ReleaseFeature: handle=%u", handle->Id);
    delete handle;
    return NVSDK_NGX_Result_Success;
}

// NVSDK_NGX_D3D12_EvaluateFeature — the hot path.  Games hit this every
// frame.  Internally just trampolines into DLSS_Evaluate which already
// knows how to dispatch to the active plugin.
__declspec(dllexport) int NVSDK_NGX_D3D12_EvaluateFeature(
    void*                          cmdList,
    NVSDK_NGX_Handle*              handle,
    adrena::NVSDK_NGX_Parameter*   params,
    void*                          /*progressCallback*/)
{
    if (!cmdList || !handle || !params) return NVSDK_NGX_Result_FAIL_InvalidParameter;
    return DLSS_Evaluate(nullptr, cmdList, params) == NVNGX_SUCCESS
        ? NVSDK_NGX_Result_Success
        : NVSDK_NGX_Result_Fail;
}

__declspec(dllexport) int NVSDK_NGX_D3D12_EvaluateFeature_C(
    void*                          cmdList,
    NVSDK_NGX_Handle*              handle,
    adrena::NVSDK_NGX_Parameter*   params,
    void*                          progressCallback)
{
    return NVSDK_NGX_D3D12_EvaluateFeature(cmdList, handle, params, progressCallback);
}

__declspec(dllexport) int NVSDK_NGX_UpdateFeature(
    const void*  /*featureCommonInfo*/,
    const void*  /*featureDiscoveryInfo*/)
{
    return NVSDK_NGX_Result_Success;
}

__declspec(dllexport) int NVSDK_NGX_GetFeatureRequirements(
    void*       /*adapterIf*/,
    const void* /*featureDiscoveryInfo*/,
    void*       /*outSupported*/)
{
    return NVSDK_NGX_Result_Success;
}

// ══════════════════════════════════════════════════════════════════
//  NVSDK_NGX_D3D11_* minimal surface — DLSS 1.0 games used D3D11.
//  We don't currently implement D3D11 upscaling (SGSR1/2 pipelines
//  are DX12-only), so report FeatureNotSupported so the game falls
//  back cleanly instead of silently failing.
// ══════════════════════════════════════════════════════════════════

__declspec(dllexport) int NVSDK_NGX_D3D11_Init(
    unsigned long long /*appId*/,
    const wchar_t*     /*appDataPath*/,
    void*              /*device*/,
    const void*        /*featureInfo*/,
    int                /*sdkVersion*/)
{
    return NVSDK_NGX_Result_FAIL_FeatureNotSupported;
}

__declspec(dllexport) int NVSDK_NGX_D3D11_Init_Ext(
    unsigned long long /*appId*/,
    const wchar_t*     /*appDataPath*/,
    void*              /*device*/,
    int                /*featureInfoVersion*/,
    const void*        /*featureInfo*/)
{
    return NVSDK_NGX_Result_FAIL_FeatureNotSupported;
}

__declspec(dllexport) int NVSDK_NGX_D3D11_Shutdown(void) {
    return NVSDK_NGX_Result_Success;
}

__declspec(dllexport) int NVSDK_NGX_D3D11_Shutdown1(void* /*device*/) {
    return NVSDK_NGX_Result_Success;
}

} // extern "C"
