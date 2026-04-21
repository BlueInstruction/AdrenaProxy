// ============================================================
//  Hand-rolled Vulkan type stub — the absolute minimum we need
//  to query device features at runtime.
//
//  We explicitly do NOT pull in the full <vulkan/vulkan.h> because
//  the MinGW cross-toolchain doesn't ship Vulkan headers by default
//  and we don't want to force the CI host to install the SDK.  The
//  Vulkan ABI is stable; the handful of structs/enums used below
//  are guaranteed to be binary-compatible with any conforming
//  loader.
// ============================================================
#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ADRENA_VK_API_VERSION_1_2 ((1u<<22) | (2u<<12) | 0u)

typedef uint32_t AdrenaVkFlags;
typedef uint32_t AdrenaVkBool32;
typedef uint64_t AdrenaVkSize;
typedef struct AdrenaVkInstance_T*       AdrenaVkInstance;
typedef struct AdrenaVkPhysicalDevice_T* AdrenaVkPhysicalDevice;

// VkResult — only the values we actually branch on.
enum AdrenaVkResult {
    ADRENA_VK_SUCCESS                 = 0,
    ADRENA_VK_ERROR_OUT_OF_HOST_MEMORY = -1,
};

// VkStructureType — only the sTypes we set.
enum AdrenaVkStructureType {
    ADRENA_VK_STRUCTURE_TYPE_APPLICATION_INFO                    = 0,
    ADRENA_VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO                = 1,
    ADRENA_VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2          = 1000059000,
    ADRENA_VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_12_FEATURES  = 1000196000,
    // VK_EXT_shader_image_atomic_int64 sType.
    ADRENA_VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_IMAGE_ATOMIC_INT64_FEATURES_EXT
                                                                 = 1000234000,
};

typedef struct AdrenaVkApplicationInfo {
    int32_t  sType;
    const void* pNext;
    const char* pApplicationName;
    uint32_t    applicationVersion;
    const char* pEngineName;
    uint32_t    engineVersion;
    uint32_t    apiVersion;
} AdrenaVkApplicationInfo;

typedef struct AdrenaVkInstanceCreateInfo {
    int32_t  sType;
    const void* pNext;
    AdrenaVkFlags flags;
    const AdrenaVkApplicationInfo* pApplicationInfo;
    uint32_t    enabledLayerCount;
    const char* const* ppEnabledLayerNames;
    uint32_t    enabledExtensionCount;
    const char* const* ppEnabledExtensionNames;
} AdrenaVkInstanceCreateInfo;

// VkPhysicalDeviceFeatures — 55 VkBool32 fields; we only care about
// shaderInt64 (field index 47).  To avoid misaligning if Khronos ever
// add a field, we consume the full 220-byte struct by raw buffer.
typedef struct AdrenaVkPhysicalDeviceFeaturesRaw {
    AdrenaVkBool32 features[55];
} AdrenaVkPhysicalDeviceFeaturesRaw;

// PhysicalDeviceFeatures2 — variant-length pNext chain.
typedef struct AdrenaVkPhysicalDeviceFeatures2 {
    int32_t sType;
    void*   pNext;
    AdrenaVkPhysicalDeviceFeaturesRaw features;
} AdrenaVkPhysicalDeviceFeatures2;

typedef struct AdrenaVkPhysicalDeviceVulkan12Features {
    int32_t sType;
    void*   pNext;
    AdrenaVkBool32 samplerMirrorClampToEdge;
    AdrenaVkBool32 drawIndirectCount;
    AdrenaVkBool32 storageBuffer8BitAccess;
    AdrenaVkBool32 uniformAndStorageBuffer8BitAccess;
    AdrenaVkBool32 storagePushConstant8;
    AdrenaVkBool32 shaderBufferInt64Atomics;
    AdrenaVkBool32 shaderSharedInt64Atomics;
    AdrenaVkBool32 shaderFloat16;
    AdrenaVkBool32 shaderInt8;
    // … the struct has 47 fields total; we only read the three we
    // care about.  Remaining fields are padded below.
    AdrenaVkBool32 _reserved[38];
} AdrenaVkPhysicalDeviceVulkan12Features;

typedef struct AdrenaVkPhysicalDeviceShaderImageAtomicInt64FeaturesEXT {
    int32_t sType;
    void*   pNext;
    AdrenaVkBool32 shaderImageInt64Atomics;
    AdrenaVkBool32 sparseImageInt64Atomics;
} AdrenaVkPhysicalDeviceShaderImageAtomicInt64FeaturesEXT;

// Function pointer typedefs.
typedef int32_t (*PFN_AdrenaVkCreateInstance)(
    const AdrenaVkInstanceCreateInfo*, const void*, AdrenaVkInstance*);
typedef void    (*PFN_AdrenaVkDestroyInstance)(AdrenaVkInstance, const void*);
typedef int32_t (*PFN_AdrenaVkEnumeratePhysicalDevices)(
    AdrenaVkInstance, uint32_t*, AdrenaVkPhysicalDevice*);
typedef void    (*PFN_AdrenaVkGetPhysicalDeviceFeatures2)(
    AdrenaVkPhysicalDevice, AdrenaVkPhysicalDeviceFeatures2*);
typedef void* (*PFN_AdrenaVkGetInstanceProcAddr)(AdrenaVkInstance, const char*);

#ifdef __cplusplus
}
#endif
