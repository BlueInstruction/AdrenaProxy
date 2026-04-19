// adrena_plugin_fsr3.dll — FSR 3.x upscaler (spatial+temporal; frame-gen
// is a separate plugin kind, not covered here).
//
// FSR 3 shares the upscaler core with FSR 2 but exposes a slightly wider
// SDK and adds reactive/transparency-composition masks. We runtime-resolve
// the FSR 3 DLLs (ffx_fsr3upscaler_x64.dll preferred) and fall back to
// passthrough when the SDK isn't installed.

#include "adrena_core/plugin_api.h"
#include "sdk_loader.h"
#include "fallback_copy.h"

#include <d3d12.h>
#include <new>

using adrena::plugin::SdkLoader;
using adrena::plugin::FallbackCopy;

namespace {

struct Fsr3Ctx {
    ID3D12Device* device = nullptr;
    SdkLoader     sdk;
    uint32_t      render_w = 0, render_h = 0, display_w = 0, display_h = 0;
    DXGI_FORMAT   output_format = DXGI_FORMAT_UNKNOWN;
    bool          sdk_context_created = false;
    // TODO(fsr3-sdk): FfxFsr3UpscalerContext, FfxInterface, etc.
};

static const char* const kFsr3Candidates[] = {
    "ffx_fsr3upscaler_x64.dll",
    "ffx_fsr3_x64.dll",
    "amd_fidelityfx_fsr3_dx12.dll",
    nullptr,
};

AdrenaUpscalerContext* Create(void* device, uint32_t api) {
    if (api != ADRENA_GFX_D3D12 || !device) return nullptr;
    auto* c = new (std::nothrow) Fsr3Ctx();
    if (!c) return nullptr;
    c->device = reinterpret_cast<ID3D12Device*>(device);
    c->sdk.TryLoadAny(kFsr3Candidates);
    return reinterpret_cast<AdrenaUpscalerContext*>(c);
}

int Init(AdrenaUpscalerContext* ctx, uint32_t out_fmt,
         uint32_t rw, uint32_t rh, uint32_t dw, uint32_t dh) {
    auto* c = reinterpret_cast<Fsr3Ctx*>(ctx);
    if (!c) return ADRENA_PLUGIN_E_INVALID_ARG;
    c->render_w = rw; c->render_h = rh;
    c->display_w = dw; c->display_h = dh;
    c->output_format = static_cast<DXGI_FORMAT>(out_fmt);
    // TODO(fsr3-sdk): ffxFsr3UpscalerContextCreate(...)
    c->sdk_context_created = c->sdk.IsLoaded();
    return ADRENA_PLUGIN_OK;
}

int Resize(AdrenaUpscalerContext* ctx, uint32_t rw, uint32_t rh,
           uint32_t dw, uint32_t dh) {
    auto* c = reinterpret_cast<Fsr3Ctx*>(ctx);
    if (!c) return ADRENA_PLUGIN_E_INVALID_ARG;
    if (rw == c->render_w && rh == c->render_h &&
        dw == c->display_w && dh == c->display_h) return ADRENA_PLUGIN_OK;
    return ADRENA_PLUGIN_E_UNSUPPORTED_API;
}

int Execute(AdrenaUpscalerContext* ctx, const AdrenaUpscaleParams* p) {
    auto* c = reinterpret_cast<Fsr3Ctx*>(ctx);
    if (!c || !p || p->abi_version != ADRENA_PLUGIN_ABI_VERSION)
        return ADRENA_PLUGIN_E_BAD_ABI;
    if (p->api != ADRENA_GFX_D3D12) return ADRENA_PLUGIN_E_UNSUPPORTED_API;
    auto* cl  = reinterpret_cast<ID3D12GraphicsCommandList*>(p->cmd_list);
    auto* src = reinterpret_cast<ID3D12Resource*>(p->input_color);
    auto* dst = reinterpret_cast<ID3D12Resource*>(p->output);
    if (!cl || !src || !dst) return ADRENA_PLUGIN_E_INVALID_ARG;

    // TODO(fsr3-sdk): ffxFsr3UpscalerContextDispatch(...)
    FallbackCopy(cl, src, dst);
    return ADRENA_PLUGIN_OK;
}

void Destroy(AdrenaUpscalerContext* ctx) {
    auto* c = reinterpret_cast<Fsr3Ctx*>(ctx);
    if (!c) return;
    // TODO(fsr3-sdk): ffxFsr3UpscalerContextDestroy(...)
    delete c;
}

const AdrenaUpscalerVTable kVTable = {
    ADRENA_PLUGIN_ABI_VERSION, &Create, &Init, &Resize, &Execute, &Destroy
};

const AdrenaPluginInfo kInfo = {
    ADRENA_PLUGIN_ABI_VERSION,
    ADRENA_PLUGIN_UPSCALER,
    "fsr3",
    "AMD FidelityFX Super Resolution 3 (Upscaler)",
    "AMD",
    "3.1.0",
    (1u << ADRENA_GFX_D3D12),
    /*spatial*/ 0, /*temporal*/ 1,
    /*dep*/ 1, /*mot*/ 1, /*jit*/ 1, /*rsv*/ 0,
    "FSR 3 upscaler (spatial+temporal, no frame-gen). Loads "
    "ffx_fsr3upscaler_x64.dll at runtime; passthrough when absent."
};

} // namespace

extern "C" {
ADRENA_PLUGIN_EXPORT const AdrenaPluginInfo*     AdrenaPlugin_GetInfo(void)            { return &kInfo; }
ADRENA_PLUGIN_EXPORT const AdrenaUpscalerVTable* AdrenaPlugin_GetUpscalerVTable(void) { return &kVTable; }
}
