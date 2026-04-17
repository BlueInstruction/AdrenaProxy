#include "adrena_core/gpu_detect.h"
#include "adrena_core/logger.h"
#include <dxgi1_6.h>
#include <algorithm>

namespace adrena {

static bool ContainsAdreno(const char* str) {
    if (!str) return false;
    std::string lower(str);
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
    return lower.find("adreno") != std::string::npos;
}

static int ClassifyTier(const std::string& desc) {
    std::string lower = desc;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
    if (lower.find("adreno 8") != std::string::npos || lower.find("8gen") != std::string::npos) return 8;
    if (lower.find("adreno 7") != std::string::npos || lower.find("7gen") != std::string::npos) return 7;
    if (lower.find("adreno 6") != std::string::npos || lower.find("6gen") != std::string::npos) return 6;
    return 0;
}

GPUInfo DetectGPU(IDXGIAdapter1* adapter) {
    GPUInfo info{};
    if (!adapter) return info;
    DXGI_ADAPTER_DESC1 desc{};
    HRESULT hr = adapter->GetDesc1(&desc);
    if (FAILED(hr)) return info;
    info.vendorId = desc.VendorId;
    info.deviceId = desc.DeviceId;
    info.dedicatedVRAM = desc.DedicatedVideoMemory;
    char narrow[256]{};
    wcstombs(narrow, desc.Description, 255);
    info.description = narrow;
    info.isAdreno = ContainsAdreno(narrow) || desc.VendorId == 0x5143;
    info.adrenoTier = ClassifyTier(info.description);
    info.waveSize = (info.adrenoTier >= 7) ? 8 : 4;
    info.fp16Support = (info.adrenoTier >= 7);
    info.fgMultiplier = (info.adrenoTier >= 7) ? 4 : 2;
    AD_LOG_I("GPU: %s (Vendor=0x%04X Device=0x%04X VRAM=%lluMB Adreno=%d Tier=%d)",
             info.description.c_str(), info.vendorId, info.deviceId,
             info.dedicatedVRAM / (1024*1024), info.isAdreno, info.adrenoTier);
    return info;
}

GPUInfo AutoDetectGPU() {
    IDXGIFactory4* factory = nullptr;
    HRESULT hr = CreateDXGIFactory2(0, IID_IDXGIFactory4, (void**)&factory);
    if (FAILED(hr)) { AD_LOG_E("AutoDetect: CreateDXGIFactory2 failed"); return {}; }
    GPUInfo best{};
    bool foundAdreno = false;
    for (UINT i = 0; ; ++i) {
        IDXGIAdapter1* adapter = nullptr;
        hr = factory->EnumAdapters1(i, &adapter);
        if (hr == DXGI_ERROR_NOT_FOUND) break;
        if (FAILED(hr)) continue;
        GPUInfo info = DetectGPU(adapter);
        if (info.isAdreno && !foundAdreno) { best = info; foundAdreno = true; }
        else if (!foundAdreno && i == 0) { best = info; }
        adapter->Release();
    }
    factory->Release();
    return best;
}

} // namespace adrena