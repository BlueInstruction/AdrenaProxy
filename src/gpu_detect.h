#pragma once
#include <string>
#include <cstdint>

namespace adrena {

enum class GPUTier { Unknown, Adreno6xx, Adreno7xx, Adreno8xx, NVIDIA, AMD, Intel, Other };

struct GPUInfo {
    GPUTier     tier = GPUTier::Unknown;
    std::string name;
    uint32_t    vendorID = 0;
    uint32_t    deviceID = 0;
    uint64_t    vramMB = 0;
    bool        isAdreno = false;
    int         recommendedFG = 1;
    int         workgroupSize = 16;
    bool        useFP16 = false;
    int         motionSearchRadius = 8;
};

GPUInfo DetectGPU();
const char* GPUTierStr(GPUTier tier);

} // namespace adrena