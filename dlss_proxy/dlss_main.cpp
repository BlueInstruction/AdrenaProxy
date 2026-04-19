#include "ngx_param.h"
#include <adrena_core/config.h>
#include <adrena_core/logger.h>
#include <adrena_core/gpu_detect.h>
#include <adrena_core/shared_state.h>
#include <adrena_core/sgsr1_pass.h>
#include <adrena_core/sgsr2_pass.h>

#include <windows.h>
#include <d3d12.h>
#include <cstdint>

static constexpr int NVNGX_SUCCESS = 0x1;
static constexpr int NVNGX_FAIL    = 0x0;

static bool g_initialized = false;
static void* g_device = nullptr;
static adrena::SGSR1Pass g_sgsr;
static adrena::SGSR2Pass g_sgsr2;
static adrena::NGXParameter* g_params = nullptr;
static bool g_historyValid = false;

// ---------------------------------------------------------------
// DLL Entry Point
// ---------------------------------------------------------------
BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID /*lpReserved*/) {
    switch (reason) {
    case DLL_PROCESS_ATTACH:
        DisableThreadLibraryCalls(hModule);
        adrena::Logger::Instance().Init("adrena_proxy.log");
        AD_LOG_I("adrenaproxy_sgsr.dll loaded (AdrenaProxy v2.0 — NVNGX Proxy)");
        {
            adrena::GPUInfo gpu = adrena::AutoDetectGPU();
            adrena::SharedState* ss = adrena::GetSharedState();
            if (ss) {
                adrena::SharedStateLock lock(&ss->lock);
                ss->is_adreno = gpu.isAdreno;
                ss->adreno_tier = gpu.adrenoTier;
            }
        }
        break;
    case DLL_PROCESS_DETACH:
        AD_LOG_I("adrenaproxy_sgsr.dll unloading");
        adrena::Logger::Instance().Shutdown();
        break;
    }
    return TRUE;
}

// ---------------------------------------------------------------
// NVNGX API
// ---------------------------------------------------------------
extern "C" {

__declspec(dllexport) int NVNGX_Init(unsigned long long appId, void* device,
                                     void* instance, const void* /*features*/) {
    AD_LOG_I("NVNGX_Init: appId=%llu device=%p instance=%p", appId, device, instance);
    if (g_initialized) return NVNGX_SUCCESS;
    if (!device) { AD_LOG_E("NVNGX_Init: null device"); return NVNGX_FAIL; }

    g_device = device;
    g_initialized = true;
    g_historyValid = false;

    adrena::SharedState* ss = adrena::GetSharedState();
    if (ss) {
        adrena::SharedStateLock lock(&ss->lock);
        ss->sgsr_active = true;
        ss->sgsr_enabled = true;
    }

    AD_LOG_I("NVNGX_Init succeeded — SGSR will handle DLSS calls");
    return NVNGX_SUCCESS;
}

__declspec(dllexport) int NVNGX_Shutdown() {
    AD_LOG_I("NVNGX_Shutdown called");
    if (g_initialized) {
        g_sgsr.Shutdown();
        g_sgsr2.Shutdown();
        if (g_params) { delete g_params; g_params = nullptr; }
        adrena::SharedState* ss = adrena::GetSharedState();
        if (ss) { adrena::SharedStateLock l(&ss->lock); ss->sgsr_active = false; ss->sgsr_enabled = false; }
        g_initialized = false;
        g_historyValid = false;
        g_device = nullptr;
    }
    return NVNGX_SUCCESS;
}

__declspec(dllexport) int NVNGX_GetParameters(void** outParams) {
    AD_LOG_I("NVNGX_GetParameters called");
    if (!outParams) return NVNGX_FAIL;
    if (!g_params) g_params = new adrena::NGXParameter();
    *outParams = g_params;
    return NVNGX_SUCCESS;
}

__declspec(dllexport) int DLSS_GetOptimalSettings(
    void* /*nvngxInstance*/,
    unsigned int targetW, unsigned int targetH,
    int perfQuality,
    unsigned int* outRenderW, unsigned int* outRenderH,
    float* outSharpness) {
    AD_LOG_I("DLSS_GetOptimalSettings: target=%ux%u quality=%d", targetW, targetH, perfQuality);

    if (!outRenderW || !outRenderH || !outSharpness) return NVNGX_FAIL;

    float scale;
    switch (perfQuality) {
    case 0:  scale = 0.33f; break;  // MaxPerf (Ultra Performance)
    case 1:  scale = 0.50f; break;  // Balanced (Performance)
    case 2:  scale = 0.67f; break;  // MaxQuality (Balanced)
    case 3:  scale = 0.77f; break;  // NativeAA (Quality)
    default: scale = 0.67f; break;
    }

    adrena::Config& cfg = adrena::GetConfig();
    if (cfg.render_scale > 0.0f && cfg.render_scale <= 1.0f) scale = cfg.render_scale;

    *outRenderW  = (unsigned int)(targetW * scale);
    *outRenderH  = (unsigned int)(targetH * scale);
    if (*outRenderW < 1) *outRenderW = 1;
    if (*outRenderH < 1) *outRenderH = 1;
    *outSharpness = cfg.sharpness;

    // Init/resize SGSR pipelines based on selected mode.
    if (g_device) {
        ID3D12Device* d3dDevice = static_cast<ID3D12Device*>(g_device);
        if (cfg.sgsr_mode == adrena::SGSRMode::SGSR2) {
            if (!g_sgsr2.IsInitialized()) {
                g_sgsr2.Init(d3dDevice, DXGI_FORMAT_R8G8B8A8_UNORM,
                             *outRenderW, *outRenderH, targetW, targetH);
                g_historyValid = false;
            } else {
                g_sgsr2.Resize(*outRenderW, *outRenderH, targetW, targetH);
                g_historyValid = false;
            }
        } else if (cfg.sgsr_mode == adrena::SGSRMode::SGSR1) {
            if (!g_sgsr.IsInitialized()) {
                g_sgsr.Init(d3dDevice, DXGI_FORMAT_R8G8B8A8_UNORM,
                            *outRenderW, *outRenderH, targetW, targetH);
            } else {
                g_sgsr.Resize(*outRenderW, *outRenderH, targetW, targetH);
            }
        }
    }

    adrena::SharedState* ss = adrena::GetSharedState();
    if (ss) {
        adrena::SharedStateLock lock(&ss->lock);
        ss->render_width = *outRenderW;
        ss->render_height = *outRenderH;
        ss->display_width = targetW;
        ss->display_height = targetH;
        ss->quality_preset = perfQuality;
        ss->sharpness = *outSharpness;
        ss->render_scale = scale;
    }

    AD_LOG_I("DLSS_GetOptimalSettings: render=%ux%u scale=%.2f sharp=%.2f",
             *outRenderW, *outRenderH, scale, *outSharpness);
    return NVNGX_SUCCESS;
}

__declspec(dllexport) int DLSS_Evaluate(
    void* /*nvngxInstance*/,
    void* cmdList,
    void* params) {
    if (!g_initialized || !cmdList || !params) return NVNGX_FAIL;

    adrena::NGXParameter* p = static_cast<adrena::NGXParameter*>(params);
    ID3D12GraphicsCommandList* cl = static_cast<ID3D12GraphicsCommandList*>(cmdList);

    // Extract D3D12 resources from parameter keys.
    // Games use various key names — handle the most common variants.
    ID3D12Resource* color = nullptr;
    ID3D12Resource* depth = nullptr;
    ID3D12Resource* motion = nullptr;
    ID3D12Resource* output = nullptr;
    float sharpness = 0.80f;
    uint32_t renderW = 0, renderH = 0;

    for (const char* key : {"Color", "DLSS.Input.Color", "Color.Input"}) {
        if (p->GetValueD3D12Res(key, &color) == 0 && color) break;
    }
    for (const char* key : {"Depth", "DLSS.Input.Depth", "Depth.Input"}) {
        if (p->GetValueD3D12Res(key, &depth) == 0 && depth) break;
    }
    for (const char* key : {"MotionVectors", "DLSS.Input.MotionVectors", "MV.Input"}) {
        if (p->GetValueD3D12Res(key, &motion) == 0 && motion) break;
    }
    for (const char* key : {"Output", "DLSS.Output", "Output.Backbuffer"}) {
        if (p->GetValueD3D12Res(key, &output) == 0 && output) break;
    }
    p->GetValueF32("Sharpness", &sharpness);
    p->GetValueUI32("Render.Subrect.Width", &renderW);
    p->GetValueUI32("Render.Subrect.Height", &renderH);

    adrena::SharedState* ss = adrena::GetSharedState();
    uint32_t displayW = renderW;
    uint32_t displayH = renderH;
    if (ss) {
        adrena::SharedStateLock lock(&ss->lock);
        if (renderW == 0) renderW = ss->render_width;
        if (renderH == 0) renderH = ss->render_height;
        displayW = ss->display_width;
        displayH = ss->display_height;
        sharpness = ss->sharpness;
    }

    AD_LOG_I("DLSS_Evaluate: color=%p depth=%p motion=%p output=%p render=%ux%u sharp=%.2f",
             color, depth, motion, output, renderW, renderH, sharpness);

    adrena::Config& cfg = adrena::GetConfig();

    // ── Path A with SGSR2 (temporal upscaling via official algorithm) ──
    if (cfg.sgsr_mode == adrena::SGSRMode::SGSR2 && g_sgsr2.IsInitialized() && output && color) {
        adrena::SGSR2Params sgsr2Params{};
        sgsr2Params.color               = color;
        sgsr2Params.depth               = depth;
        sgsr2Params.motion              = motion;
        sgsr2Params.output              = output;
        sgsr2Params.sharpness           = sharpness;
        sgsr2Params.renderWidth         = renderW;
        sgsr2Params.renderHeight        = renderH;
        sgsr2Params.displayWidth        = displayW ? displayW : renderW;
        sgsr2Params.displayHeight       = displayH ? displayH : renderH;
        sgsr2Params.resetHistory        = !g_historyValid;
        sgsr2Params.preExposure         = 1.0f;
        sgsr2Params.minLerpContribution = 0.15f;
        sgsr2Params.bSameCamera         = true;
        sgsr2Params.cameraFovAngleHor   = 1.0472f;
        sgsr2Params.cameraNear          = 0.01f;
        sgsr2Params.cameraFar           = 1000.0f;

        g_sgsr2.Execute(cl, sgsr2Params);
        g_historyValid = true;
        AD_LOG_I("DLSS_Evaluate: SGSR2 official temporal upscaling executed");
        return NVNGX_SUCCESS;
    }

    // ── Path A with SGSR1 (spatial upscaling via official algorithm) ──
    if (cfg.sgsr_mode == adrena::SGSRMode::SGSR1 && g_sgsr.IsInitialized() && output && color) {
        adrena::SGSRParams sgsrParams{};
        sgsrParams.color         = color;
        sgsrParams.depth         = depth;
        sgsrParams.motion        = motion;
        sgsrParams.output        = output;
        sgsrParams.sharpness     = sharpness;
        sgsrParams.renderWidth   = renderW;
        sgsrParams.renderHeight  = renderH;
        sgsrParams.displayWidth  = displayW ? displayW : renderW;
        sgsrParams.displayHeight = displayH ? displayH : renderH;

        g_sgsr.Execute(cl, sgsrParams);
        AD_LOG_I("DLSS_Evaluate: SGSR1 official spatial upscaling executed");
        return NVNGX_SUCCESS;
    }

    return NVNGX_SUCCESS;
}

// ---------------------------------------------------------------
// C API parameter accessor exports
// Some games use these directly instead of the C++ virtual interface
// ---------------------------------------------------------------

__declspec(dllexport) int NVSDK_NGX_Parameter_SetValue_UI32(void* params, const char* key, uint32_t val) {
    if (!params) return -1;
    return static_cast<adrena::NGXParameter*>(params)->SetValueUI32(key, val);
}
__declspec(dllexport) int NVSDK_NGX_Parameter_SetValue_I32(void* params, const char* key, int32_t val) {
    if (!params) return -1;
    return static_cast<adrena::NGXParameter*>(params)->SetValueI32(key, val);
}
__declspec(dllexport) int NVSDK_NGX_Parameter_SetValue_F32(void* params, const char* key, float val) {
    if (!params) return -1;
    return static_cast<adrena::NGXParameter*>(params)->SetValueF32(key, val);
}
__declspec(dllexport) int NVSDK_NGX_Parameter_SetValue_F64(void* params, const char* key, double val) {
    if (!params) return -1;
    return static_cast<adrena::NGXParameter*>(params)->SetValueF64(key, val);
}
__declspec(dllexport) int NVSDK_NGX_Parameter_SetValue_UI64(void* params, const char* key, uint64_t val) {
    if (!params) return -1;
    return static_cast<adrena::NGXParameter*>(params)->SetValueUI64(key, val);
}
__declspec(dllexport) int NVSDK_NGX_Parameter_SetValue_Ptr(void* params, const char* key, void* val) {
    if (!params) return -1;
    return static_cast<adrena::NGXParameter*>(params)->SetValuePtr(key, val);
}
__declspec(dllexport) int NVSDK_NGX_Parameter_SetValue_D3d12Resource(void* params, const char* key, ID3D12Resource* val) {
    if (!params) return -1;
    return static_cast<adrena::NGXParameter*>(params)->SetValueD3D12Res(key, val);
}

__declspec(dllexport) int NVSDK_NGX_Parameter_GetValue_UI32(void* params, const char* key, uint32_t* val) {
    if (!params) return -1;
    return static_cast<adrena::NGXParameter*>(params)->GetValueUI32(key, val);
}
__declspec(dllexport) int NVSDK_NGX_Parameter_GetValue_I32(void* params, const char* key, int32_t* val) {
    if (!params) return -1;
    return static_cast<adrena::NGXParameter*>(params)->GetValueI32(key, val);
}
__declspec(dllexport) int NVSDK_NGX_Parameter_GetValue_F32(void* params, const char* key, float* val) {
    if (!params) return -1;
    return static_cast<adrena::NGXParameter*>(params)->GetValueF32(key, val);
}
__declspec(dllexport) int NVSDK_NGX_Parameter_GetValue_F64(void* params, const char* key, double* val) {
    if (!params) return -1;
    return static_cast<adrena::NGXParameter*>(params)->GetValueF64(key, val);
}
__declspec(dllexport) int NVSDK_NGX_Parameter_GetValue_UI64(void* params, const char* key, uint64_t* val) {
    if (!params) return -1;
    return static_cast<adrena::NGXParameter*>(params)->GetValueUI64(key, val);
}
__declspec(dllexport) int NVSDK_NGX_Parameter_GetValue_Ptr(void* params, const char* key, void** val) {
    if (!params) return -1;
    return static_cast<adrena::NGXParameter*>(params)->GetValuePtr(key, val);
}
__declspec(dllexport) int NVSDK_NGX_Parameter_GetValue_D3d12Resource(void* params, const char* key, ID3D12Resource** val) {
    if (!params) return -1;
    return static_cast<adrena::NGXParameter*>(params)->GetValueD3D12Res(key, val);
}

__declspec(dllexport) void NVSDK_NGX_Parameter_Reset(void* params) {
    if (params) static_cast<adrena::NGXParameter*>(params)->Reset();
}
__declspec(dllexport) void NVSDK_NGX_Parameter_Destroy(void* params) {
    if (params) delete static_cast<adrena::NGXParameter*>(params);
}

} // extern "C"
