#include "adrena_core/plugin_api.h"
#include <ffx_fsr2.h>
#include <dx12/ffx_fsr2_dx12.h>
#include <d3d12.h>
#include <windows.h>
#include <cstdlib>
#include <cstring>
#include <cstdint>

struct Fsr2Ctx {
    FfxFsr2Context   ctx;
    void*            scratch;
    size_t           scratchSize;
    ID3D12Device*    device;
    uint32_t         renderW, renderH;
    uint32_t         displayW, displayH;
    bool             initialized;
    uint64_t         frameID;
};

static const AdrenaPluginDesc s_desc = {
    ADRENA_PLUGIN_API_VERSION,
    "fsr2",
    "AMD FidelityFX Super Resolution 2",
    "2.2.0",
    1, 1, 1
};

static bool CreateContext(Fsr2Ctx* c,
                          uint32_t rW, uint32_t rH,
                          uint32_t dW, uint32_t dH) {
    c->scratchSize = ffxFsr2GetScratchMemorySizeDX12(c->device);
    c->scratch     = malloc(c->scratchSize);
    if (!c->scratch) return false;

    FfxFsr2Interface iface{};
    if (ffxFsr2GetInterfaceDX12(&iface, c->device,
                                 c->scratch, c->scratchSize) != FFX_OK) {
        free(c->scratch);
        c->scratch = nullptr;
        return false;
    }

    FfxFsr2ContextDescription desc{};
    desc.flags         = FFX_FSR2_ENABLE_HIGH_DYNAMIC_RANGE
                       | FFX_FSR2_ENABLE_AUTO_EXPOSURE;
    desc.maxRenderSize = { rW, rH };
    desc.displaySize   = { dW, dH };
    desc.callbacks     = iface;
    desc.device        = ffxGetDeviceDX12(c->device);

    if (ffxFsr2ContextCreate(&c->ctx, &desc) != FFX_OK) {
        free(c->scratch);
        c->scratch = nullptr;
        return false;
    }

    c->renderW  = rW;  c->renderH  = rH;
    c->displayW = dW;  c->displayH = dH;
    c->frameID  = 0;
    return true;
}

static void DestroyContext(Fsr2Ctx* c) {
    if (c->initialized) {
        ffxFsr2ContextDestroy(&c->ctx);
        free(c->scratch);
        c->scratch     = nullptr;
        c->initialized = false;
    }
}

extern "C" {

__declspec(dllexport)
const AdrenaPluginDesc* AdrenaPlugin_GetDesc(void) { return &s_desc; }

__declspec(dllexport)
AdrenaPluginCtx AdrenaPlugin_Create(void) { return new Fsr2Ctx{}; }

__declspec(dllexport)
void AdrenaPlugin_Destroy(AdrenaPluginCtx ctx) {
    delete static_cast<Fsr2Ctx*>(ctx);
}

__declspec(dllexport)
int AdrenaPlugin_Init(AdrenaPluginCtx ctx, ID3D12Device* dev, DXGI_FORMAT /*fmt*/,
                      uint32_t rW, uint32_t rH, uint32_t dW, uint32_t dH) {
    auto* c    = static_cast<Fsr2Ctx*>(ctx);
    c->device  = dev;
    c->initialized = CreateContext(c, rW, rH, dW, dH);
    return c->initialized ? 0 : 1;
}

__declspec(dllexport)
int AdrenaPlugin_Resize(AdrenaPluginCtx ctx,
                        uint32_t rW, uint32_t rH, uint32_t dW, uint32_t dH) {
    auto* c = static_cast<Fsr2Ctx*>(ctx);
    DestroyContext(c);
    c->initialized = CreateContext(c, rW, rH, dW, dH);
    return c->initialized ? 0 : 1;
}

__declspec(dllexport)
int AdrenaPlugin_Execute(AdrenaPluginCtx ctx,
                         ID3D12GraphicsCommandList* cl,
                         const AdrenaUpscaleParams* p) {
    auto* c = static_cast<Fsr2Ctx*>(ctx);
    if (!c->initialized) return 1;

    FfxFsr2DispatchDescription disp{};
    disp.commandList  = ffxGetCommandListDX12(cl);
    disp.color        = ffxGetResourceDX12(&c->ctx, p->color,  nullptr,
                                            FFX_RESOURCE_STATE_PIXEL_COMPUTE_READ);
    disp.depth        = ffxGetResourceDX12(&c->ctx, p->depth,  nullptr,
                                            FFX_RESOURCE_STATE_PIXEL_COMPUTE_READ);
    disp.motionVectors= ffxGetResourceDX12(&c->ctx, p->motion, nullptr,
                                            FFX_RESOURCE_STATE_PIXEL_COMPUTE_READ);
    disp.output       = ffxGetResourceDX12(&c->ctx, p->output, nullptr,
                                            FFX_RESOURCE_STATE_UNORDERED_ACCESS);

    disp.motionVectorScale.x  = static_cast<float>(p->renderWidth)  * -0.5f;
    disp.motionVectorScale.y  = static_cast<float>(p->renderHeight) * -0.5f;
    disp.renderSize           = { p->renderWidth, p->renderHeight };
    disp.enableSharpening     = p->sharpness > 0.0f ? FFX_TRUE : FFX_FALSE;
    disp.sharpness            = p->sharpness;
    disp.frameTimeDelta       = 16.67f;
    disp.preExposure          = p->preExposure > 0.0f ? p->preExposure : 1.0f;
    disp.reset                = p->resetHistory ? FFX_TRUE : FFX_FALSE;
    disp.cameraNear           = p->cameraNear  > 0.0f ? p->cameraNear  : 0.01f;
    disp.cameraFar            = p->cameraFar   > 0.0f ? p->cameraFar   : 1000.0f;
    disp.cameraFovAngleVertical = p->cameraFovH > 0.0f ? p->cameraFovH : 1.0472f;
    disp.frameID              = ++c->frameID;

    return (ffxFsr2ContextDispatch(&c->ctx, &disp) == FFX_OK) ? 0 : 1;
}

__declspec(dllexport)
void AdrenaPlugin_Shutdown(AdrenaPluginCtx ctx) {
    DestroyContext(static_cast<Fsr2Ctx*>(ctx));
}

} // extern "C"
