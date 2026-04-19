// adrena_plugin_fsr4.dll — FSR 4 ML-based temporal upscaler.
//
// FSR 4 requires an RDNA 4 GPU (Radeon RX 9070 family and later) and the
// official amdxcffx64.dll runtime. The plugin always loads, probes for
// RDNA 4 via D3D12 feature-level queries, and refuses (E_UNSUPPORTED_API)
// when the hardware or runtime isn't there — the host will then fall
// back to a different selected plugin or disable upscaling cleanly.
//
// Hardware check is a best-effort heuristic since the AMD VendorID/
// feature tuple is what gates FSR 4 in practice. We also support the
// developer escape hatch `ADRENA_FORCE_FSR4=1` for testing on non-RDNA4
// hardware where the SDK itself still loads.

#include "adrena_core/plugin_api.h"
#include "sdk_loader.h"
#include "fallback_copy.h"

#include <d3d12.h>
#include <dxgi1_6.h>
#include <new>
#include <cstdlib>

using adrena::plugin::SdkLoader;
using adrena::plugin::FallbackCopy;

namespace {

constexpr UINT kAmdVendorId = 0x1002;

struct Fsr4Ctx {
    ID3D12Device* device = nullptr;
    SdkLoader     sdk;
    bool          hw_supported = false;
    uint32_t      render_w = 0, render_h = 0, display_w = 0, display_h = 0;
    DXGI_FORMAT   output_format = DXGI_FORMAT_UNKNOWN;
    bool          sdk_context_created = false;
    // TODO(fsr4-sdk): FfxFsr4Context once SDK is vendored.
};

static const char* const kFsr4Candidates[] = {
    "amdxcffx64.dll",
    "ffx_fsr4_x64.dll",
    nullptr,
};

static bool ProbeRdna4(ID3D12Device* device) {
    if (const char* env = std::getenv("ADRENA_FORCE_FSR4"); env && *env == '1')
        return true;
    if (!device) return false;

    LUID luid = device->GetAdapterLuid();
    IDXGIFactory4* factory = nullptr;
    if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory))) || !factory) return false;

    IDXGIAdapter1* adapter = nullptr;
    bool rdna4 = false;
    if (SUCCEEDED(factory->EnumAdapterByLuid(luid, IID_PPV_ARGS(&adapter))) && adapter) {
        DXGI_ADAPTER_DESC1 desc{};
        if (SUCCEEDED(adapter->GetDesc1(&desc)) && desc.VendorId == kAmdVendorId) {
            // RDNA 4 dies have DeviceId >= 0x7470 as of launch. This is a
            // rough heuristic; real detection will prefer the AMD AGS call
            // once that dependency is brought in.
            rdna4 = desc.DeviceId >= 0x7470;
        }
        adapter->Release();
    }
    factory->Release();
    return rdna4;
}

AdrenaUpscalerContext* Create(void* device, uint32_t api) {
    if (api != ADRENA_GFX_D3D12 || !device) return nullptr;
    auto* c = new (std::nothrow) Fsr4Ctx();
    if (!c) return nullptr;
    c->device       = reinterpret_cast<ID3D12Device*>(device);
    c->hw_supported = ProbeRdna4(c->device);
    if (c->hw_supported) c->sdk.TryLoadAny(kFsr4Candidates);
    return reinterpret_cast<AdrenaUpscalerContext*>(c);
}

int Init(AdrenaUpscalerContext* ctx, uint32_t out_fmt,
         uint32_t rw, uint32_t rh, uint32_t dw, uint32_t dh) {
    auto* c = reinterpret_cast<Fsr4Ctx*>(ctx);
    if (!c) return ADRENA_PLUGIN_E_INVALID_ARG;
    if (!c->hw_supported || !c->sdk.IsLoaded()) return ADRENA_PLUGIN_E_UNSUPPORTED_API;
    c->render_w = rw; c->render_h = rh;
    c->display_w = dw; c->display_h = dh;
    c->output_format = static_cast<DXGI_FORMAT>(out_fmt);
    // TODO(fsr4-sdk): ffxFsr4ContextCreate(...)
    c->sdk_context_created = true;
    return ADRENA_PLUGIN_OK;
}

int Resize(AdrenaUpscalerContext* ctx, uint32_t rw, uint32_t rh,
           uint32_t dw, uint32_t dh) {
    auto* c = reinterpret_cast<Fsr4Ctx*>(ctx);
    if (!c) return ADRENA_PLUGIN_E_INVALID_ARG;
    if (rw == c->render_w && rh == c->render_h &&
        dw == c->display_w && dh == c->display_h) return ADRENA_PLUGIN_OK;
    return ADRENA_PLUGIN_E_UNSUPPORTED_API;
}

int Execute(AdrenaUpscalerContext* ctx, const AdrenaUpscaleParams* p) {
    auto* c = reinterpret_cast<Fsr4Ctx*>(ctx);
    if (!c || !p || p->abi_version != ADRENA_PLUGIN_ABI_VERSION)
        return ADRENA_PLUGIN_E_BAD_ABI;
    if (!c->sdk_context_created) return ADRENA_PLUGIN_E_NOT_INITIALIZED;
    if (p->api != ADRENA_GFX_D3D12) return ADRENA_PLUGIN_E_UNSUPPORTED_API;
    auto* cl  = reinterpret_cast<ID3D12GraphicsCommandList*>(p->cmd_list);
    auto* src = reinterpret_cast<ID3D12Resource*>(p->input_color);
    auto* dst = reinterpret_cast<ID3D12Resource*>(p->output);
    if (!cl || !src || !dst) return ADRENA_PLUGIN_E_INVALID_ARG;

    // TODO(fsr4-sdk): ffxFsr4ContextDispatch(...)
    FallbackCopy(cl, src, dst);
    return ADRENA_PLUGIN_OK;
}

void Destroy(AdrenaUpscalerContext* ctx) {
    auto* c = reinterpret_cast<Fsr4Ctx*>(ctx);
    if (!c) return;
    // TODO(fsr4-sdk): ffxFsr4ContextDestroy(...)
    delete c;
}

const AdrenaUpscalerVTable kVTable = {
    ADRENA_PLUGIN_ABI_VERSION, &Create, &Init, &Resize, &Execute, &Destroy
};

const AdrenaPluginInfo kInfo = {
    ADRENA_PLUGIN_ABI_VERSION,
    ADRENA_PLUGIN_UPSCALER,
    "fsr4",
    "AMD FidelityFX Super Resolution 4 (ML)",
    "AMD",
    "4.0.0",
    (1u << ADRENA_GFX_D3D12),
    /*spatial*/ 0, /*temporal*/ 1,
    /*dep*/ 1, /*mot*/ 1, /*jit*/ 1, /*rsv*/ 0,
    "Machine-learning temporal upscaler. Requires RDNA 4 hardware and "
    "amdxcffx64.dll. Set ADRENA_FORCE_FSR4=1 to bypass the hardware check "
    "for development."
};

} // namespace

extern "C" {
ADRENA_PLUGIN_EXPORT const AdrenaPluginInfo*     AdrenaPlugin_GetInfo(void)            { return &kInfo; }
ADRENA_PLUGIN_EXPORT const AdrenaUpscalerVTable* AdrenaPlugin_GetUpscalerVTable(void) { return &kVTable; }
}
