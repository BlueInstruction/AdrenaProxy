#include "gpu_detect.h"
#include "logger.h"
#include <dxgi.h>
#include <algorithm>

namespace adrena {

const char* GPUTierStr(GPUTier tier)
{
    switch (tier) {
    case GPUTier::Adreno6xx: return "Adreno 6xx";
    case GPUTier::Adreno7xx: return "Adreno 7xx";
    case GPUTier::Adreno8xx: return "Adreno 8xx";
    case GPUTier::NVIDIA:    return "NVIDIA";
    case GPUTier::AMD:       return "AMD";
    case GPUTier::Intel:     return "Intel";
    case GPUTier::Other:     return "Other";
    default:                 return "Unknown";
    }
}

static GPUTier ClassifyByVendor(uint32_t vendorID, uint32_t deviceID, const std::string& name)
{
    // Qualcomm vendor ID = 0x5143
    if (vendorID == 0x5143) {
        // Try to detect Adreno generation from device ID or name
        if (name.find("830") != std::string::npos || name.find("840") != std::string::npos)
            return GPUTier::Adreno8xx;
        if (name.find("730") != std::string::npos || name.find("740") != std::string::npos || name.find("750") != std::string::npos)
            return GPUTier::Adreno7xx;
        if (name.find("630") != std::string::npos || name.find("640") != std::string::npos ||
            name.find("650") != std::string::npos || name.find("660") != std::string::npos ||
            name.find("680") != std::string::npos || name.find("690") != std::string::npos)
            return GPUTier::Adreno6xx;

        // Fallback by device ID ranges
        if (deviceID >= 0x08030000) return GPUTier::Adreno8xx;
        if (deviceID >= 0x07030000) return GPUTier::Adreno7xx;
        if (deviceID >= 0x06030000) return GPUTier::Adreno6xx;

        // Generic Qualcomm
        return GPUTier::Adreno7xx; // assume mid-range
    }

    // NVIDIA = 0x10DE
    if (vendorID == 0x10DE) return GPUTier::NVIDIA;
    // AMD = 0x1002 / 0x1022
    if (vendorID == 0x1002 || vendorID == 0x1022) return GPUTier::AMD;
    // Intel = 0x8086
    if (vendorID == 0x8086) return GPUTier::Intel;

    return GPUTier::Other;
}

GPUInfo DetectGPU()
{
    GPUInfo info = {};

    IDXGIFactory1* factory = nullptr;
    HRESULT hr = CreateDXGIFactory1(__uuidof(IDXGIFactory1), (void**)&factory);
    if (FAILED(hr)) {
        AD_LOG_E("Failed to create DXGI factory for GPU detection");
        return info;
    }

    IDXGIAdapter1* adapter = nullptr;
    for (UINT i = 0; factory->EnumAdapters1(i, &adapter) != DXGI_ERROR_NOT_FOUND; i++)
    {
        DXGI_ADAPTER_DESC1 desc;
        adapter->GetDesc1(&desc);

        if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) {
            adapter->Release();
            continue;
        }

        info.vendorID = desc.VendorId;
        info.deviceID = desc.DeviceId;
        info.vramMB = desc.DedicatedVideoMemory / (1024 * 1024);

        char nameBuf[256] = {};
        wcstombs(nameBuf, desc.Description, sizeof(nameBuf) - 1);
        info.name = nameBuf;

        AD_LOG_I("GPU[%u]: %s (Vendor=0x%04X Device=0x%04X VRAM=%lluMB)",
                 i, nameBuf, desc.VendorId, desc.DeviceId, info.vramMB);

        info.tier = ClassifyByVendor(desc.VendorId, desc.DeviceId, info.name);
        info.isAdreno = (desc.VendorId == 0x5143);

        // Set tier-specific defaults
        switch (info.tier) {
        case GPUTier::Adreno6xx:
            info.recommendedFG = 2;
            info.workgroupSize = 8;
            info.useFP16 = true;
            info.motionSearchRadius = 4;
            break;
        case GPUTier::Adreno7xx:
            info.recommendedFG = 2;
            info.workgroupSize = 8;
            info.useFP16 = true;
            info.motionSearchRadius = 8;
            break;
        case GPUTier::Adreno8xx:
            info.recommendedFG = 3;
            info.workgroupSize = 8;
            info.useFP16 = true;
            info.motionSearchRadius = 16;
            break;
        default:
            info.recommendedFG = 2;
            info.workgroupSize = 16;
            info.useFP16 = false;
            info.motionSearchRadius = 12;
            break;
        }

        // Use first hardware adapter
        adapter->Release();
        break;
    }

    factory->Release();

    AD_LOG_I("Detected GPU: %s (Tier=%s Adreno=%d FG=%dx WG=%d FP16=%d)",
             info.name.c_str(), GPUTierStr(info.tier), info.isAdreno,
             info.recommendedFG, info.workgroupSize, info.useFP16);

    return info;
}

} // namespace adrena
