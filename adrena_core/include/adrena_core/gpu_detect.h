#pragma once
#include <cstdint>
#include <string>

struct IDXGIAdapter1;

namespace adrena {

struct GPUInfo {
    bool        isAdreno      = false;
    int         adrenoTier    = 0;       // 6=6xx, 7=7xx, 8=8xx
    std::string description   = "";
    uint32_t    vendorId      = 0;
    uint32_t    deviceId      = 0;
    uint64_t    dedicatedVRAM = 0;
    int         waveSize      = 8;
    bool        fp16Support   = false;
    int         fgMultiplier  = 2;       // Max FG multiplier for this tier
};

GPUInfo DetectGPU(IDXGIAdapter1* adapter);
GPUInfo AutoDetectGPU();

} // namespace adrena