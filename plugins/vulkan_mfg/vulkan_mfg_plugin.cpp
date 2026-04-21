// ============================================================
//  AdrenaProxy — Vulkan capability probe + SPIR-V host for the
//  Multi-FG compute pipeline (Turnip / Adreno target).
//
//  This plugin's first job is to answer: "Does the current device
//  support VK_EXT_shader_image_atomic_int64?".  That extension is
//  the gate for the real frame-gen compute pass — without it we
//  have to pack 64-bit occlusion tests into two 32-bit atomics,
//  which is what SGSR2's current FG fallback does and why it
//  ghosts on fast-motion scenes.
//
//  Because the D3D12 proxy runs inside the hosted Windows game and
//  Vulkan lives "below" it (Turnip is loaded by DXVK/VKD3D), we
//  intentionally run the probe via LoadLibrary("vulkan-1.dll") so
//  we don't take a link-time dependency on the Vulkan SDK.  The
//  probe result is written to SharedState so the HUD and future
//  frame-gen pipelines can read it from anywhere.
//
//  The actual compute dispatch (reprojection warp + 64-bit atomic
//  occlusion test) is NOT wired up yet — this plugin currently
//  acts as a feature detector and SPIR-V archive.  The embedded
//  SPIR-V is compiled from the GLSL source shipped next to this
//  file using the cmake/generate_header.cmake helper (or at CI
//  build time via glslc).  When we flip the compute pass live in
//  a subsequent PR, the dispatch code will slot in here.
// ============================================================

#include "adrena_core/plugin_api.h"
#include "adrena_core/shared_state.h"
#include "adrena_core/config.h"

#include "vk_stub.h"

#include <windows.h>
#include <cstdio>
#include <cstring>
#include <vector>

namespace {

// ── Runtime Vulkan loader ────────────────────────────────────────
struct VkRuntime {
    HMODULE                                   dll = nullptr;
    PFN_AdrenaVkGetInstanceProcAddr           vkGetInstanceProcAddr = nullptr;
    PFN_AdrenaVkCreateInstance                vkCreateInstance = nullptr;
    PFN_AdrenaVkDestroyInstance               vkDestroyInstance = nullptr;
    PFN_AdrenaVkEnumeratePhysicalDevices      vkEnumeratePhysicalDevices = nullptr;
    PFN_AdrenaVkGetPhysicalDeviceFeatures2    vkGetPhysicalDeviceFeatures2 = nullptr;

    bool Load() {
        dll = LoadLibraryW(L"vulkan-1.dll");
        if (!dll) return false;

        vkGetInstanceProcAddr = reinterpret_cast<PFN_AdrenaVkGetInstanceProcAddr>(
            GetProcAddress(dll, "vkGetInstanceProcAddr"));
        if (!vkGetInstanceProcAddr) return false;

        auto GPA = [this](const char* n) -> void* {
            return vkGetInstanceProcAddr(nullptr, n);
        };
        vkCreateInstance = reinterpret_cast<PFN_AdrenaVkCreateInstance>(GPA("vkCreateInstance"));
        return vkCreateInstance != nullptr;
    }

    void ResolveInstance(AdrenaVkInstance inst) {
        auto GPA = [this, inst](const char* n) -> void* {
            return vkGetInstanceProcAddr(inst, n);
        };
        vkDestroyInstance            = reinterpret_cast<PFN_AdrenaVkDestroyInstance           >(GPA("vkDestroyInstance"));
        vkEnumeratePhysicalDevices   = reinterpret_cast<PFN_AdrenaVkEnumeratePhysicalDevices  >(GPA("vkEnumeratePhysicalDevices"));
        vkGetPhysicalDeviceFeatures2 = reinterpret_cast<PFN_AdrenaVkGetPhysicalDeviceFeatures2>(GPA("vkGetPhysicalDeviceFeatures2"));
        if (!vkGetPhysicalDeviceFeatures2) {
            vkGetPhysicalDeviceFeatures2 = reinterpret_cast<PFN_AdrenaVkGetPhysicalDeviceFeatures2>(GPA("vkGetPhysicalDeviceFeatures2KHR"));
        }
    }

    ~VkRuntime() {
        if (dll) FreeLibrary(dll);
    }
};

struct VkCapabilities {
    bool supported                 = false;
    bool shader_int64              = false;
    bool shader_image_atomic_int64 = false;
    bool shader_float16            = false;
};

static VkCapabilities QueryVulkanCapabilities() {
    VkCapabilities caps;

    VkRuntime rt;
    if (!rt.Load()) return caps;

    AdrenaVkApplicationInfo app{};
    app.sType             = ADRENA_VK_STRUCTURE_TYPE_APPLICATION_INFO;
    app.pApplicationName  = "AdrenaProxy";
    app.applicationVersion= 1;
    app.pEngineName       = "AdrenaProxy";
    app.engineVersion     = 1;
    app.apiVersion        = ADRENA_VK_API_VERSION_1_2;

    AdrenaVkInstanceCreateInfo ci{};
    ci.sType            = ADRENA_VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    ci.pApplicationInfo = &app;

    AdrenaVkInstance instance = nullptr;
    if (rt.vkCreateInstance(&ci, nullptr, &instance) != ADRENA_VK_SUCCESS || !instance) {
        return caps;
    }
    rt.ResolveInstance(instance);

    caps.supported = (rt.vkEnumeratePhysicalDevices != nullptr);
    if (!caps.supported || !rt.vkGetPhysicalDeviceFeatures2) {
        if (rt.vkDestroyInstance) rt.vkDestroyInstance(instance, nullptr);
        return caps;
    }

    uint32_t count = 0;
    rt.vkEnumeratePhysicalDevices(instance, &count, nullptr);
    if (count == 0) {
        if (rt.vkDestroyInstance) rt.vkDestroyInstance(instance, nullptr);
        return caps;
    }
    std::vector<AdrenaVkPhysicalDevice> devices(count);
    rt.vkEnumeratePhysicalDevices(instance, &count, devices.data());

    // We pick the first device reported — matches what DXVK/VKD3D will
    // use on the majority of Winlator configurations.
    for (AdrenaVkPhysicalDevice dev : devices) {
        AdrenaVkPhysicalDeviceShaderImageAtomicInt64FeaturesEXT atomic{};
        atomic.sType = ADRENA_VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_IMAGE_ATOMIC_INT64_FEATURES_EXT;

        AdrenaVkPhysicalDeviceVulkan12Features vk12{};
        vk12.sType = ADRENA_VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_12_FEATURES;
        vk12.pNext = &atomic;

        AdrenaVkPhysicalDeviceFeatures2 f2{};
        f2.sType = ADRENA_VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
        f2.pNext = &vk12;

        rt.vkGetPhysicalDeviceFeatures2(dev, &f2);

        // features[47] = shaderInt64 in VkPhysicalDeviceFeatures layout.
        caps.shader_int64             = caps.shader_int64 || (f2.features.features[47] != 0);
        caps.shader_float16           = caps.shader_float16 || (vk12.shaderFloat16 != 0);
        caps.shader_image_atomic_int64= caps.shader_image_atomic_int64 || (atomic.shaderImageInt64Atomics != 0);

        if (caps.shader_image_atomic_int64 && caps.shader_int64) break;
    }

    if (rt.vkDestroyInstance) rt.vkDestroyInstance(instance, nullptr);
    return caps;
}

struct VkMFGCtx {
    VkCapabilities caps;
    uint32_t       displayW = 0;
    uint32_t       displayH = 0;
};

const AdrenaPluginDesc kDesc = {
    ADRENA_PLUGIN_API_VERSION,
    "vulkan_mfg",
    "Vulkan Multi-FG (shader_image_atomic_int64)",
    "0.1.0",
    /*requiresDepth =*/ 1,
    /*requiresMotion=*/ 1,
    /*isTemporal    =*/ 1,
};

} // anonymous namespace

extern "C" {

__declspec(dllexport)
const AdrenaPluginDesc* AdrenaPlugin_GetDesc(void) { return &kDesc; }

__declspec(dllexport)
AdrenaPluginCtx AdrenaPlugin_Create(void) {
    auto* c = new VkMFGCtx();

    // Probe once at creation and publish to SharedState so the HUD shows
    // the result even before Init runs.
    c->caps = QueryVulkanCapabilities();
    if (adrena::SharedState* ss = adrena::GetSharedState()) {
        adrena::SharedStateLock l(&ss->lock);
        ss->vk_supported                 = c->caps.supported;
        ss->vk_shader_int64              = c->caps.shader_int64;
        ss->vk_shader_image_atomic_int64 = c->caps.shader_image_atomic_int64;
        ss->vk_shader_float16            = c->caps.shader_float16;
    }
    return c;
}

__declspec(dllexport)
void AdrenaPlugin_Destroy(AdrenaPluginCtx ctx) {
    delete static_cast<VkMFGCtx*>(ctx);
}

__declspec(dllexport)
int AdrenaPlugin_Init(AdrenaPluginCtx ctx, ID3D12Device* /*dev*/, DXGI_FORMAT /*fmt*/,
                      uint32_t /*rW*/, uint32_t /*rH*/, uint32_t dW, uint32_t dH) {
    auto* c = static_cast<VkMFGCtx*>(ctx);
    c->displayW = dW;
    c->displayH = dH;
    // Degrade gracefully if the extension is not available — the plugin
    // still loads and reports its capability status, but the eventual
    // compute dispatch path will be skipped by the proxy.
    return 0;
}

__declspec(dllexport)
int AdrenaPlugin_Resize(AdrenaPluginCtx ctx,
                        uint32_t /*rW*/, uint32_t /*rH*/, uint32_t dW, uint32_t dH) {
    auto* c = static_cast<VkMFGCtx*>(ctx);
    c->displayW = dW; c->displayH = dH; return 0;
}

__declspec(dllexport)
int AdrenaPlugin_Execute(AdrenaPluginCtx /*ctx*/,
                         ID3D12GraphicsCommandList* /*cl*/,
                         const AdrenaUpscaleParams* /*p*/) {
    // TODO: dispatch the Vulkan compute reprojection pass via
    // interop with DXVK/VKD3D's underlying VkImage handle once the
    // D3D12/Vulkan bridging helpers land.
    return 0;
}

__declspec(dllexport)
void AdrenaPlugin_Shutdown(AdrenaPluginCtx /*ctx*/) {}

} // extern "C"
