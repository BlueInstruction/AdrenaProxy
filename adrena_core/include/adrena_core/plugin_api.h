#pragma once
#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <d3d12.h>
#include <dxgi.h>

#define ADRENA_PLUGIN_API_VERSION 1

typedef struct AdrenaUpscaleParams {
    ID3D12Resource* color;
    ID3D12Resource* depth;
    ID3D12Resource* motion;
    ID3D12Resource* output;
    float           sharpness;
    uint32_t        renderWidth;
    uint32_t        renderHeight;
    uint32_t        displayWidth;
    uint32_t        displayHeight;
    int             resetHistory;
    float           preExposure;
    float           minLerpContribution;
    int             sameCamera;
    float           cameraFovH;
    float           cameraNear;
    float           cameraFar;
} AdrenaUpscaleParams;

typedef struct AdrenaPluginDesc {
    uint32_t    apiVersion;
    const char* id;
    const char* displayName;
    const char* version;
    int         requiresDepth;
    int         requiresMotion;
    int         isTemporal;
} AdrenaPluginDesc;

typedef void* AdrenaPluginCtx;

typedef const AdrenaPluginDesc* (*PFN_AdrenaPlugin_GetDesc)(void);
typedef AdrenaPluginCtx         (*PFN_AdrenaPlugin_Create)(void);
typedef void                    (*PFN_AdrenaPlugin_Destroy)(AdrenaPluginCtx);
typedef int  (*PFN_AdrenaPlugin_Init)(AdrenaPluginCtx, ID3D12Device*, DXGI_FORMAT,
                                      uint32_t, uint32_t, uint32_t, uint32_t);
typedef int  (*PFN_AdrenaPlugin_Resize)(AdrenaPluginCtx, uint32_t, uint32_t, uint32_t, uint32_t);
typedef int  (*PFN_AdrenaPlugin_Execute)(AdrenaPluginCtx, ID3D12GraphicsCommandList*,
                                         const AdrenaUpscaleParams*);
typedef void (*PFN_AdrenaPlugin_Shutdown)(AdrenaPluginCtx);

#define ADRENA_PLUGIN_EXPORT_SYMBOL_GetDesc  "AdrenaPlugin_GetDesc"
#define ADRENA_PLUGIN_EXPORT_SYMBOL_Create   "AdrenaPlugin_Create"
#define ADRENA_PLUGIN_EXPORT_SYMBOL_Destroy  "AdrenaPlugin_Destroy"
#define ADRENA_PLUGIN_EXPORT_SYMBOL_Init     "AdrenaPlugin_Init"
#define ADRENA_PLUGIN_EXPORT_SYMBOL_Resize   "AdrenaPlugin_Resize"
#define ADRENA_PLUGIN_EXPORT_SYMBOL_Execute  "AdrenaPlugin_Execute"
#define ADRENA_PLUGIN_EXPORT_SYMBOL_Shutdown "AdrenaPlugin_Shutdown"

#ifdef __cplusplus
}
#endif
