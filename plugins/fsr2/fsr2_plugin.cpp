// adrena_plugin_fsr2.dll — AMD FidelityFX Super Resolution 2 backend.
//
// Build-time we have no dependency on the FFX SDK — the runtime loads
// `ffx_fsr2_x64.dll` / `ffx_backend_dx12_x64.dll` via SdkLoader when the
// game ships them. If the SDK isn't present, the plugin still loads and
// runs `FallbackCopy` so the integration path stays testable. Real SDK
// wiring is behind the `TODO(fsr2-sdk)` markers.
//
// Spec sources:
//   https://github.com/GPUOpen-LibrariesAndSDKs/FidelityFX-SDK
//   https://gpuopen.com/fidelityfx-superresolution-2/

#include "adrena_core/plugin_api.h"

#include "sdk_loader.h"
#include "fallback_copy.h"

#include <d3d12.h>

#include <cstring>
#include <new>

using adrena::plugin::SdkLoader;
using adrena::plugin::FallbackCopy;

namespace {

struct Fsr2Ctx {
    ID3D12Device* device        = nullptr;
    SdkLoader     sdk;
    uint32_t      render_w      = 0;
    uint32_t      render_h      = 0;
    uint32_t      display_w     = 0;
    uint32_t      display_h     = 0;
    DXGI_FORMAT   output_format = DXGI_FORMAT_UNKNOWN;
    bool          sdk_context_created = false;

    // TODO(fsr2-sdk): store FfxFsr2Context / FfxInterface once the SDK
    // headers are vendored. Treat as opaque bytes for now to keep the
    // plugin DLL buildable without the SDK in-tree.
};

// Candidate DLLs shipped with FFX SDK / FSR 2 runtimes, newest first.
static const char* const kFsr2Candidates[] = {
    "ffx_fsr2_x64.dll",
    "ffx_backend_dx12_x64.dll",
    "amd_fidelityfx_dx12.dll",
    nullptr,
};

AdrenaUpscalerContext* Create(void* device, uint32_t api) {
    if (api != ADRENA_GFX_D3D12 || !device) return nullptr;
    auto* c = new (std::nothrow) Fsr2Ctx();
    if (!c) return nullptr;
    c->device = reinterpret_cast<ID3D12Device*>(device);
    if (c->sdk.TryLoadAny(kFsr2Candidates)) {
        // TODO(fsr2-sdk): resolve ffxFsr2ContextCreate / ffxFsr2ContextDispatch
        // / ffxFsr2ContextDestroy via c->sdk.GetProc<>(...) and wire them.
    }
    return reinterpret_cast<AdrenaUpscalerContext*>(c);
}

int Init(AdrenaUpscalerContext* ctx, uint32_t out_fmt,
         uint32_t rw, uint32_t rh, uint32_t dw, uint32_t dh) {
    auto* c = reinterpret_cast<Fsr2Ctx*>(ctx);
    if (!c) return ADRENA_PLUGIN_E_INVALID_ARG;
    c->render_w = rw;       c->render_h = rh;
    c->display_w = dw;      c->display_h = dh;
    c->output_format = static_cast<DXGI_FORMAT>(out_fmt);
    // TODO(fsr2-sdk): ffxFsr2ContextCreate(&desc) once SDK entries are
    // resolved. For now we just mark the context ready.
    c->sdk_context_created = c->sdk.IsLoaded();
    return ADRENA_PLUGIN_OK;
}

int Resize(AdrenaUpscalerContext* ctx, uint32_t rw, uint32_t rh,
           uint32_t dw, uint32_t dh) {
    auto* c = reinterpret_cast<Fsr2Ctx*>(ctx);
    if (!c) return ADRENA_PLUGIN_E_INVALID_ARG;
    if (rw == c->render_w && rh == c->render_h &&
        dw == c->display_w && dh == c->display_h) {
        return ADRENA_PLUGIN_OK;
    }
    // Simplest correct behavior: force host to destroy+create. We can do
    // better once the FFX context is wired (it supports resize natively).
    return ADRENA_PLUGIN_E_UNSUPPORTED_API;
}

int Execute(AdrenaUpscalerContext* ctx, const AdrenaUpscaleParams* p) {
    auto* c = reinterpret_cast<Fsr2Ctx*>(ctx);
    if (!c || !p || p->abi_version != ADRENA_PLUGIN_ABI_VERSION)
        return ADRENA_PLUGIN_E_BAD_ABI;
    if (p->api != ADRENA_GFX_D3D12) return ADRENA_PLUGIN_E_UNSUPPORTED_API;

    auto* cl  = reinterpret_cast<ID3D12GraphicsCommandList*>(p->cmd_list);
    auto* src = reinterpret_cast<ID3D12Resource*>(p->input_color);
    auto* dst = reinterpret_cast<ID3D12Resource*>(p->output);
    if (!cl || !src || !dst) return ADRENA_PLUGIN_E_INVALID_ARG;

    if (c->sdk_context_created) {
        // TODO(fsr2-sdk): build an FfxFsr2DispatchDescription from p
        // (color/depth/motion/exposure, jitter, delta-time, sharpness,
        // reset, camera params) and call ffxFsr2ContextDispatch(). Apply
        // the built-in RCAS sharpen via the same SDK entry.
        FallbackCopy(cl, src, dst);  // placeholder until the TODO above is done
        return ADRENA_PLUGIN_OK;
    }

    // SDK not available at the game. Run the visible-safe fallback so the
    // rest of the integration still renders something.
    FallbackCopy(cl, src, dst);
    return ADRENA_PLUGIN_OK;
}

void Destroy(AdrenaUpscalerContext* ctx) {
    auto* c = reinterpret_cast<Fsr2Ctx*>(ctx);
    if (!c) return;
    // TODO(fsr2-sdk): ffxFsr2ContextDestroy(&c->ctx) before freeing.
    delete c;
}

const AdrenaUpscalerVTable kVTable = {
    ADRENA_PLUGIN_ABI_VERSION,
    &Create, &Init, &Resize, &Execute, &Destroy
};

const AdrenaPluginInfo kInfo = {
    /* abi_version      */ ADRENA_PLUGIN_ABI_VERSION,
    /* kind             */ ADRENA_PLUGIN_UPSCALER,
    /* name             */ "fsr2",
    /* display_name     */ "AMD FidelityFX Super Resolution 2",
    /* vendor           */ "AMD",
    /* version          */ "2.2.2",
    /* supported_apis   */ (1u << ADRENA_GFX_D3D12),
    /* supports_spatial */ 0,
    /* supports_temporal*/ 1,
    /* requires_depth   */ 1,
    /* requires_motion  */ 1,
    /* requires_jitter  */ 1,
    /* reserved_flags   */ 0,
    /* description      */
    "Temporal upscaler with RCAS sharpen. Loads ffx_fsr2_x64.dll if present; "
    "falls back to passthrough if the FFX SDK runtime isn't shipped with the "
    "game."
};

} // namespace

extern "C" {

ADRENA_PLUGIN_EXPORT const AdrenaPluginInfo* AdrenaPlugin_GetInfo(void) {
    return &kInfo;
}

ADRENA_PLUGIN_EXPORT const AdrenaUpscalerVTable* AdrenaPlugin_GetUpscalerVTable(void) {
    return &kVTable;
}

} // extern "C"
