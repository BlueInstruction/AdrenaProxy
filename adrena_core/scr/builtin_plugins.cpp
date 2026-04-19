// Built-in plugin adapters — wrap the in-tree SGSR1 / SGSR2 passes in the
// AdrenaProxy plugin ABI so the host can drive them through the same
// interface it uses for externally-loaded DLL plugins.
//
// These adapters live inside adrena_core.a so no DLL loading is required
// at runtime; they are registered at startup via
// RegisterBuiltinPlugins().

#include "adrena_core/plugin_api.h"
#include "adrena_core/plugin_manager.h"
#include "adrena_core/sgsr1_pass.h"
#include "adrena_core/sgsr2_pass.h"
#include "adrena_core/logger.h"

#include <d3d12.h>

#include <cstring>

namespace adrena {

// ═══════════════════════════════════════════════════════════════════════
//  Helpers shared by the D3D12 adapters
// ═══════════════════════════════════════════════════════════════════════

namespace {

inline bool IsD3D12(uint32_t api) { return api == ADRENA_GFX_D3D12; }

inline ID3D12GraphicsCommandList* AsCmdList(void* p) {
    return reinterpret_cast<ID3D12GraphicsCommandList*>(p);
}
inline ID3D12Resource* AsRes(void* p) {
    return reinterpret_cast<ID3D12Resource*>(p);
}

} // namespace

// ═══════════════════════════════════════════════════════════════════════
//  SGSR1 adapter
// ═══════════════════════════════════════════════════════════════════════

struct Sgsr1Ctx {
    SGSR1Pass    pass;
    ID3D12Device* device = nullptr;
};

static AdrenaUpscalerContext* Sgsr1_Create(void* device, uint32_t api) {
    if (!IsD3D12(api) || !device) return nullptr;
    auto* c = new (std::nothrow) Sgsr1Ctx();
    if (!c) return nullptr;
    c->device = reinterpret_cast<ID3D12Device*>(device);
    return reinterpret_cast<AdrenaUpscalerContext*>(c);
}

static int Sgsr1_Init(AdrenaUpscalerContext* ctx, uint32_t out_format,
                      uint32_t rw, uint32_t rh, uint32_t dw, uint32_t dh) {
    auto* c = reinterpret_cast<Sgsr1Ctx*>(ctx);
    if (!c) return ADRENA_PLUGIN_E_INVALID_ARG;
    const DXGI_FORMAT fmt = static_cast<DXGI_FORMAT>(out_format);
    return c->pass.Init(c->device, fmt, rw, rh, dw, dh)
        ? ADRENA_PLUGIN_OK
        : ADRENA_PLUGIN_E_INTERNAL;
}

static int Sgsr1_Resize(AdrenaUpscalerContext* ctx,
                        uint32_t rw, uint32_t rh, uint32_t dw, uint32_t dh) {
    auto* c = reinterpret_cast<Sgsr1Ctx*>(ctx);
    if (!c) return ADRENA_PLUGIN_E_INVALID_ARG;
    return c->pass.Resize(rw, rh, dw, dh)
        ? ADRENA_PLUGIN_OK
        : ADRENA_PLUGIN_E_INTERNAL;
}

static int Sgsr1_Execute(AdrenaUpscalerContext* ctx,
                         const AdrenaUpscaleParams* p) {
    auto* c = reinterpret_cast<Sgsr1Ctx*>(ctx);
    if (!c || !p || p->abi_version != ADRENA_PLUGIN_ABI_VERSION)
        return ADRENA_PLUGIN_E_BAD_ABI;
    if (!IsD3D12(p->api)) return ADRENA_PLUGIN_E_UNSUPPORTED_API;
    if (!c->pass.IsInitialized()) return ADRENA_PLUGIN_E_NOT_INITIALIZED;

    SGSRParams sp{};
    sp.color         = AsRes(p->input_color);
    sp.depth         = AsRes(p->input_depth);
    sp.motion        = AsRes(p->input_motion);
    sp.output        = AsRes(p->output);
    sp.sharpness     = p->sharpness;
    sp.renderWidth   = p->render_w;
    sp.renderHeight  = p->render_h;
    sp.displayWidth  = p->display_w ? p->display_w : p->render_w;
    sp.displayHeight = p->display_h ? p->display_h : p->render_h;

    c->pass.Execute(AsCmdList(p->cmd_list), sp);
    return ADRENA_PLUGIN_OK;
}

static void Sgsr1_Destroy(AdrenaUpscalerContext* ctx) {
    auto* c = reinterpret_cast<Sgsr1Ctx*>(ctx);
    if (!c) return;
    c->pass.Shutdown();
    delete c;
}

static const AdrenaUpscalerVTable kSgsr1VTable = {
    ADRENA_PLUGIN_ABI_VERSION,
    &Sgsr1_Create,
    &Sgsr1_Init,
    &Sgsr1_Resize,
    &Sgsr1_Execute,
    &Sgsr1_Destroy,
};

static const AdrenaPluginInfo kSgsr1Info = {
    /* abi_version     */ ADRENA_PLUGIN_ABI_VERSION,
    /* kind            */ ADRENA_PLUGIN_UPSCALER,
    /* name            */ "sgsr1",
    /* display_name    */ "Snapdragon GSR 1 (Spatial)",
    /* vendor          */ "Qualcomm",
    /* version         */ "1.0.0",
    /* supported_apis  */ (1u << ADRENA_GFX_D3D12),
    /* supports_spatial*/ 1,
    /* supports_temporal*/ 0,
    /* requires_depth  */ 0,
    /* requires_motion */ 0,
    /* requires_jitter */ 0,
    /* reserved_flags  */ 0,
    /* description     */ "Fast single-pass edge-directed spatial upscaler.",
};

static const AdrenaPluginInfo*     Sgsr1_GetInfo()            { return &kSgsr1Info; }
static const AdrenaUpscalerVTable* Sgsr1_GetUpscalerVTable()  { return &kSgsr1VTable; }

// ═══════════════════════════════════════════════════════════════════════
//  SGSR2 adapter
// ═══════════════════════════════════════════════════════════════════════

struct Sgsr2Ctx {
    SGSR2Pass     pass;
    ID3D12Device* device = nullptr;
};

static AdrenaUpscalerContext* Sgsr2_Create(void* device, uint32_t api) {
    if (!IsD3D12(api) || !device) return nullptr;
    auto* c = new (std::nothrow) Sgsr2Ctx();
    if (!c) return nullptr;
    c->device = reinterpret_cast<ID3D12Device*>(device);
    return reinterpret_cast<AdrenaUpscalerContext*>(c);
}

static int Sgsr2_Init(AdrenaUpscalerContext* ctx, uint32_t out_format,
                      uint32_t rw, uint32_t rh, uint32_t dw, uint32_t dh) {
    auto* c = reinterpret_cast<Sgsr2Ctx*>(ctx);
    if (!c) return ADRENA_PLUGIN_E_INVALID_ARG;
    const DXGI_FORMAT fmt = static_cast<DXGI_FORMAT>(out_format);
    return c->pass.Init(c->device, fmt, rw, rh, dw, dh)
        ? ADRENA_PLUGIN_OK
        : ADRENA_PLUGIN_E_INTERNAL;
}

static int Sgsr2_Resize(AdrenaUpscalerContext* ctx,
                        uint32_t rw, uint32_t rh, uint32_t dw, uint32_t dh) {
    auto* c = reinterpret_cast<Sgsr2Ctx*>(ctx);
    if (!c) return ADRENA_PLUGIN_E_INVALID_ARG;
    return c->pass.Resize(rw, rh, dw, dh)
        ? ADRENA_PLUGIN_OK
        : ADRENA_PLUGIN_E_INTERNAL;
}

static int Sgsr2_Execute(AdrenaUpscalerContext* ctx,
                         const AdrenaUpscaleParams* p) {
    auto* c = reinterpret_cast<Sgsr2Ctx*>(ctx);
    if (!c || !p || p->abi_version != ADRENA_PLUGIN_ABI_VERSION)
        return ADRENA_PLUGIN_E_BAD_ABI;
    if (!IsD3D12(p->api)) return ADRENA_PLUGIN_E_UNSUPPORTED_API;
    if (!c->pass.IsInitialized()) return ADRENA_PLUGIN_E_NOT_INITIALIZED;

    SGSR2Params sp{};
    sp.color               = AsRes(p->input_color);
    sp.depth               = AsRes(p->input_depth);
    sp.motion              = AsRes(p->input_motion);
    sp.exposure            = AsRes(p->input_exposure);
    sp.output              = AsRes(p->output);
    sp.sharpness           = p->sharpness;
    sp.renderWidth         = p->render_w;
    sp.renderHeight        = p->render_h;
    sp.displayWidth        = p->display_w ? p->display_w : p->render_w;
    sp.displayHeight       = p->display_h ? p->display_h : p->render_h;
    sp.resetHistory        = p->reset_history != 0;
    sp.jitterX             = p->jitter_x;
    sp.jitterY             = p->jitter_y;
    sp.preExposure         = p->pre_exposure > 0.0f ? p->pre_exposure : 1.0f;
    sp.cameraNear          = p->camera_near > 0.0f ? p->camera_near : 0.01f;
    sp.cameraFar           = p->camera_far  > 0.0f ? p->camera_far  : 1000.0f;
    sp.cameraFovAngleHor   = p->camera_fov_h_radians > 0.0f ? p->camera_fov_h_radians : 1.0472f;
    sp.minLerpContribution = p->min_lerp_contribution > 0.0f ? p->min_lerp_contribution : 0.15f;
    sp.bSameCamera         = p->same_camera != 0;
    sp.motionVectorScale   = p->motion_vector_scale != 0 ? p->motion_vector_scale : 1;
    sp.deltaTime           = p->delta_time_seconds;
    std::memcpy(sp.clipToPrevClip, p->clip_to_prev_clip, sizeof(float) * 16);

    c->pass.Execute(AsCmdList(p->cmd_list), sp);
    return ADRENA_PLUGIN_OK;
}

static void Sgsr2_Destroy(AdrenaUpscalerContext* ctx) {
    auto* c = reinterpret_cast<Sgsr2Ctx*>(ctx);
    if (!c) return;
    c->pass.Shutdown();
    delete c;
}

static const AdrenaUpscalerVTable kSgsr2VTable = {
    ADRENA_PLUGIN_ABI_VERSION,
    &Sgsr2_Create,
    &Sgsr2_Init,
    &Sgsr2_Resize,
    &Sgsr2_Execute,
    &Sgsr2_Destroy,
};

static const AdrenaPluginInfo kSgsr2Info = {
    /* abi_version     */ ADRENA_PLUGIN_ABI_VERSION,
    /* kind            */ ADRENA_PLUGIN_UPSCALER,
    /* name            */ "sgsr2",
    /* display_name    */ "Snapdragon GSR 2 (Temporal)",
    /* vendor          */ "Qualcomm",
    /* version         */ "2.0.0",
    /* supported_apis  */ (1u << ADRENA_GFX_D3D12),
    /* supports_spatial*/ 0,
    /* supports_temporal*/ 1,
    /* requires_depth  */ 1,
    /* requires_motion */ 1,
    /* requires_jitter */ 1,
    /* reserved_flags  */ 0,
    /* description     */ "Two-pass temporal upscaler with motion vectors.",
};

static const AdrenaPluginInfo*     Sgsr2_GetInfo()            { return &kSgsr2Info; }
static const AdrenaUpscalerVTable* Sgsr2_GetUpscalerVTable()  { return &kSgsr2VTable; }

// ═══════════════════════════════════════════════════════════════════════
//  Registration entry point
// ═══════════════════════════════════════════════════════════════════════

void RegisterBuiltinPlugins() {
    PluginManager& pm = PluginManager::Instance();

    AdrenaPluginRegistration sgsr1{ &Sgsr1_GetInfo, &Sgsr1_GetUpscalerVTable };
    pm.RegisterBuiltin(sgsr1);

    AdrenaPluginRegistration sgsr2{ &Sgsr2_GetInfo, &Sgsr2_GetUpscalerVTable };
    pm.RegisterBuiltin(sgsr2);
}

} // namespace adrena
