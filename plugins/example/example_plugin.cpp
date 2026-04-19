#include "adrena_core/plugin_api.h"
#include <d3d12.h>
#include <cstring>

struct ExampleCtx {
    ID3D12Device* device;
    uint32_t      renderW, renderH;
    uint32_t      displayW, displayH;
};

static const AdrenaPluginDesc s_desc = {
    ADRENA_PLUGIN_API_VERSION,
    "example",
    "Example Passthrough Plugin",
    "0.1.0",
    0,
    0,
    0
};

extern "C" {

__declspec(dllexport)
const AdrenaPluginDesc* AdrenaPlugin_GetDesc(void) {
    return &s_desc;
}

__declspec(dllexport)
AdrenaPluginCtx AdrenaPlugin_Create(void) {
    return new ExampleCtx{};
}

__declspec(dllexport)
void AdrenaPlugin_Destroy(AdrenaPluginCtx ctx) {
    delete static_cast<ExampleCtx*>(ctx);
}

__declspec(dllexport)
int AdrenaPlugin_Init(AdrenaPluginCtx ctx, ID3D12Device* dev, DXGI_FORMAT /*fmt*/,
                      uint32_t rW, uint32_t rH, uint32_t dW, uint32_t dH) {
    auto* c     = static_cast<ExampleCtx*>(ctx);
    c->device   = dev;
    c->renderW  = rW;  c->renderH  = rH;
    c->displayW = dW;  c->displayH = dH;
    return 0;
}

__declspec(dllexport)
int AdrenaPlugin_Resize(AdrenaPluginCtx ctx,
                        uint32_t rW, uint32_t rH, uint32_t dW, uint32_t dH) {
    auto* c     = static_cast<ExampleCtx*>(ctx);
    c->renderW  = rW;  c->renderH  = rH;
    c->displayW = dW;  c->displayH = dH;
    return 0;
}

__declspec(dllexport)
int AdrenaPlugin_Execute(AdrenaPluginCtx /*ctx*/,
                         ID3D12GraphicsCommandList* /*cl*/,
                         const AdrenaUpscaleParams* /*p*/) {
    // Passthrough — does nothing. Replace with your upscaler logic.
    return 0;
}

__declspec(dllexport)
void AdrenaPlugin_Shutdown(AdrenaPluginCtx /*ctx*/) {}

} // extern "C"
