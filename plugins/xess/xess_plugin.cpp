#include "adrena_core/plugin_api.h"
#include <xess/xess.h>
#include <xess/xess_d3d12.h>
#include <d3d12.h>
#include <cstdlib>
#include <cstring>
#include <cstdint>

struct XessCtx {
    xess_context_handle_t handle;
    ID3D12Device*         device;
    uint32_t              renderW, renderH;
    uint32_t              displayW, displayH;
    bool                  initialized;
    uint64_t              frameID;
};

static const AdrenaPluginDesc s_desc = {
    ADRENA_PLUGIN_API_VERSION,
    "xess",
    "Intel XeSS",
    "1.3.0",
    1, 1, 1
};

static xess_quality_settings_t PickQuality(uint32_t rW, uint32_t dW) {
    float ratio = static_cast<float>(dW) / static_cast<float>(rW);
    if (ratio >= 2.0f) return XESS_QUALITY_SETTING_ULTRA_PERFORMANCE;
    if (ratio >= 1.7f) return XESS_QUALITY_SETTING_PERFORMANCE;
    if (ratio >= 1.5f) return XESS_QUALITY_SETTING_BALANCED;
    return XESS_QUALITY_SETTING_QUALITY;
}

static bool CreateContext(XessCtx* c,
                           uint32_t rW, uint32_t rH,
                           uint32_t dW, uint32_t dH) {
    if (xessD3D12CreateContext(c->device, &c->handle) != XESS_RESULT_SUCCESS)
        return false;

    xess_d3d12_init_params_t params{};
    params.outputResolution.x = dW;
    params.outputResolution.y = dH;
    params.qualitySetting     = PickQuality(rW, dW);
    params.initFlags          = XESS_INIT_FLAG_INVERTED_DEPTH
                              | XESS_INIT_FLAG_ENABLE_AUTOEXPOSURE;

    if (xessD3D12Init(c->handle, &params) != XESS_RESULT_SUCCESS) {
        xessDestroyContext(c->handle);
        return false;
    }

    c->renderW  = rW;  c->renderH  = rH;
    c->displayW = dW;  c->displayH = dH;
    c->frameID  = 0;
    return true;
}

static void DestroyContext(XessCtx* c) {
    if (c->initialized) {
        xessDestroyContext(c->handle);
        c->initialized = false;
    }
}

extern "C" {

__declspec(dllexport)
const AdrenaPluginDesc* AdrenaPlugin_GetDesc(void) { return &s_desc; }

__declspec(dllexport)
AdrenaPluginCtx AdrenaPlugin_Create(void) { return new XessCtx{}; }

__declspec(dllexport)
void AdrenaPlugin_Destroy(AdrenaPluginCtx ctx) {
    delete static_cast<XessCtx*>(ctx);
}

__declspec(dllexport)
int AdrenaPlugin_Init(AdrenaPluginCtx ctx, ID3D12Device* dev, DXGI_FORMAT /*fmt*/,
                      uint32_t rW, uint32_t rH, uint32_t dW, uint32_t dH) {
    auto* c    = static_cast<XessCtx*>(ctx);
    c->device  = dev;
    c->initialized = CreateContext(c, rW, rH, dW, dH);
    return c->initialized ? 0 : 1;
}

__declspec(dllexport)
int AdrenaPlugin_Resize(AdrenaPluginCtx ctx,
                        uint32_t rW, uint32_t rH, uint32_t dW, uint32_t dH) {
    auto* c = static_cast<XessCtx*>(ctx);
    DestroyContext(c);
    c->initialized = CreateContext(c, rW, rH, dW, dH);
    return c->initialized ? 0 : 1;
}

__declspec(dllexport)
int AdrenaPlugin_Execute(AdrenaPluginCtx ctx,
                         ID3D12GraphicsCommandList* cl,
                         const AdrenaUpscaleParams* p) {
    auto* c = static_cast<XessCtx*>(ctx);
    if (!c->initialized) return 1;

    xess_d3d12_execute_params_t exec{};
    exec.pColorTexture       = p->color;
    exec.pVelocityTexture    = p->motion;
    exec.pDepthTexture       = p->depth;
    exec.pOutputTexture      = p->output;
    exec.jitterOffsetX       = 0.0f;
    exec.jitterOffsetY       = 0.0f;
    exec.exposureScale       = p->preExposure > 0.0f ? p->preExposure : 1.0f;
    exec.resetHistory        = p->resetHistory != 0;
    exec.inputWidth          = p->renderWidth;
    exec.inputHeight         = p->renderHeight;
    exec.frameIndex          = static_cast<uint32_t>(++c->frameID);

    return (xessD3D12Execute(c->handle, cl, &exec) == XESS_RESULT_SUCCESS) ? 0 : 1;
}

__declspec(dllexport)
void AdrenaPlugin_Shutdown(AdrenaPluginCtx ctx) {
    DestroyContext(static_cast<XessCtx*>(ctx));
}

} // extern "C"
