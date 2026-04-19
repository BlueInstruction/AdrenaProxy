// AdrenaProxy plugin ABI — stable C interface for external upscaler / frame-
// generation plugins shipped as DLLs alongside the proxy.
//
// Design rules:
//   * C linkage only at the ABI boundary (no C++ types, no RTTI, no STL).
//   * Every struct starts with an explicit uint32_t abi_version so we can
//     evolve the schema without breaking old plugins.
//   * All pointers the host passes are owned by the host; the plugin must
//     not free them.
//   * All pointers the plugin returns from create() are owned by the plugin
//     and must be released via the matching destroy() entry point.
//   * Return codes: 0 = success, negative = error (see ADRENA_PLUGIN_E_*).
//
// Built-in plugins (SGSR1, SGSR2) use this same interface via a thin C
// wrapper so the host never has to treat them specially.
#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ADRENA_PLUGIN_ABI_VERSION 1u

/* ───────────────────────── Error codes ───────────────────────── */

#define ADRENA_PLUGIN_OK                    0
#define ADRENA_PLUGIN_E_UNSUPPORTED_API    -1
#define ADRENA_PLUGIN_E_BAD_ABI            -2
#define ADRENA_PLUGIN_E_OOM                -3
#define ADRENA_PLUGIN_E_DEVICE_LOST        -4
#define ADRENA_PLUGIN_E_INVALID_ARG        -5
#define ADRENA_PLUGIN_E_NOT_INITIALIZED    -6
#define ADRENA_PLUGIN_E_INTERNAL           -7

/* ───────────────────────── Capability flags ───────────────────────── */

typedef enum AdrenaPluginKind {
    ADRENA_PLUGIN_UPSCALER  = 1,
    ADRENA_PLUGIN_FRAMEGEN  = 2,
} AdrenaPluginKind;

typedef enum AdrenaGfxAPI {
    ADRENA_GFX_D3D12  = 1,
    ADRENA_GFX_VULKAN = 2,
    ADRENA_GFX_D3D11  = 3,
} AdrenaGfxAPI;

/* ───────────────────────── Plugin descriptor ───────────────────────── */

typedef struct AdrenaPluginInfo {
    uint32_t    abi_version;       /* must be ADRENA_PLUGIN_ABI_VERSION */
    uint32_t    kind;              /* AdrenaPluginKind */
    const char* name;              /* short id, [a-z0-9_-]+, e.g. "sgsr2"  */
    const char* display_name;      /* human readable */
    const char* vendor;            /* e.g. "Qualcomm", "AMD", "Intel"     */
    const char* version;           /* semver string                        */

    /* Bitfield of 1 << AdrenaGfxAPI values that this plugin supports.     */
    uint32_t    supported_apis;

    /* Feature flags. */
    uint32_t    supports_spatial  : 1;
    uint32_t    supports_temporal : 1;
    uint32_t    requires_depth    : 1;
    uint32_t    requires_motion   : 1;
    uint32_t    requires_jitter   : 1;
    uint32_t    reserved_flags    : 27;

    /* Free text shown in UI / overlay. Null-safe. */
    const char* description;
} AdrenaPluginInfo;

/* ───────────────────────── Upscale params ───────────────────────── */

typedef struct AdrenaUpscaleParams {
    uint32_t abi_version;  /* must be ADRENA_PLUGIN_ABI_VERSION */
    uint32_t api;          /* AdrenaGfxAPI */

    /* Graphics-API native handles. For D3D12 these are pointers to
     * ID3D12GraphicsCommandList and ID3D12Resource. For Vulkan they are
     * VkCommandBuffer / VkImage handles (cast to uintptr_t-wide pointers).
     */
    void* cmd_list;
    void* input_color;
    void* input_depth;
    void* input_motion;
    void* input_exposure;
    void* output;

    uint32_t render_w;
    uint32_t render_h;
    uint32_t display_w;
    uint32_t display_h;

    float    sharpness;
    float    jitter_x;
    float    jitter_y;
    float    delta_time_seconds;
    float    pre_exposure;
    float    camera_near;
    float    camera_far;
    float    camera_fov_h_radians;
    float    min_lerp_contribution;
    int32_t  motion_vector_scale;

    uint32_t reset_history     : 1;
    uint32_t same_camera       : 1;
    uint32_t reserved_flags    : 30;

    /* Row-major 4x4 clip-to-previous-clip matrix. */
    float    clip_to_prev_clip[16];
} AdrenaUpscaleParams;

/* ───────────────────────── Upscaler vtable ───────────────────────── */

/* Opaque instance type; the plugin defines its layout. */
typedef struct AdrenaUpscalerContext AdrenaUpscalerContext;

typedef struct AdrenaUpscalerVTable {
    uint32_t abi_version;  /* must be ADRENA_PLUGIN_ABI_VERSION */

    /* Create an upscaler context bound to the given native device handle.
     * Returns NULL on failure. */
    AdrenaUpscalerContext* (*create)(void* device, uint32_t api);

    /* Initialize / reinitialize internal resources for the given render and
     * display extents. out_format is an AdrenaGfxAPI-specific format value
     * (DXGI_FORMAT on D3D12, VkFormat on Vulkan). */
    int (*init)(AdrenaUpscalerContext* ctx,
                uint32_t out_format,
                uint32_t render_w, uint32_t render_h,
                uint32_t display_w, uint32_t display_h);

    /* Resize without a full recreate. Optional; may return
     * ADRENA_PLUGIN_E_UNSUPPORTED_API to force host to destroy+create. */
    int (*resize)(AdrenaUpscalerContext* ctx,
                  uint32_t render_w, uint32_t render_h,
                  uint32_t display_w, uint32_t display_h);

    /* Execute one upscale pass. */
    int (*execute)(AdrenaUpscalerContext* ctx,
                   const AdrenaUpscaleParams* params);

    /* Destroy context. Safe to call with NULL. */
    void (*destroy)(AdrenaUpscalerContext* ctx);
} AdrenaUpscalerVTable;

/* ───────────────────────── DLL entry points ─────────────────────────
 *
 * Each plugin DLL must export these symbols with C linkage.
 * Built-in plugins register an AdrenaPluginRegistration struct instead
 * (see plugin_manager.h).
 */

#if defined(_WIN32)
#  define ADRENA_PLUGIN_EXPORT __declspec(dllexport)
#else
#  define ADRENA_PLUGIN_EXPORT __attribute__((visibility("default")))
#endif

/* Return the plugin's static descriptor. Called at load time. */
typedef const AdrenaPluginInfo* (*AdrenaPlugin_GetInfoFn)(void);

/* Return the upscaler vtable. Only called when info->kind is
 * ADRENA_PLUGIN_UPSCALER. May return NULL if kind doesn't match. */
typedef const AdrenaUpscalerVTable* (*AdrenaPlugin_GetUpscalerVTableFn)(void);

/* Symbol names expected in the plugin DLL. */
#define ADRENA_PLUGIN_SYM_GET_INFO             "AdrenaPlugin_GetInfo"
#define ADRENA_PLUGIN_SYM_GET_UPSCALER_VTABLE  "AdrenaPlugin_GetUpscalerVTable"

#ifdef __cplusplus
} /* extern "C" */
#endif
