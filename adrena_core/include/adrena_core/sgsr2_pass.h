#pragma once
#include <d3d12.h>
#include <dxgi1_4.h>
#include <cstdint>

namespace adrena {

// SGSR2 Temporal Upscaling — Experimental Stub
//
// SGSR2 requires depth buffer + motion vectors from the game engine,
// which are only available through the DLSS proxy path (DLSS_Evaluate).
// When a DLSS-compatible game provides these textures, SGSR2 can perform
// temporal accumulation for higher quality than SGSR1 spatial-only.
//
// Current status: Pipeline structure is ready, temporal accumulation
// logic needs implementation based on Qualcomm's SGSR2 algorithm.

struct SGSR2Params {
    ID3D12Resource* color       = nullptr;  // Current low-res color
    ID3D12Resource* depth       = nullptr;  // Current depth buffer
    ID3D12Resource* motion      = nullptr;  // Motion vectors
    ID3D12Resource* exposure    = nullptr;  // Optional exposure texture
    ID3D12Resource* output      = nullptr;  // Upscaled output target

    float    deltaTime          = 0.0f;
    uint32_t renderWidth        = 0;
    uint32_t renderHeight       = 0;
    uint32_t displayWidth       = 0;
    uint32_t displayHeight      = 0;
    float    sharpness          = 0.80f;
    bool     resetHistory       = false;

    float    jitterX            = 0.0f;     // Sub-pixel jitter offset
    float    jitterY            = 0.0f;
    float    cameraNear         = 0.01f;
    float    cameraFar          = 1000.0f;
    float    cameraFovY         = 1.0472f;  // 60 degrees

    int32_t  motionVectorScale  = 1;        // 0=pixel, 1=screen-space
};

class SGSR2Pass {
public:
    SGSR2Pass() = default;
    ~SGSR2Pass();

    SGSR2Pass(const SGSR2Pass&) = delete;
    SGSR2Pass& operator=(const SGSR2Pass&) = delete;

    bool Init(ID3D12Device* device, DXGI_FORMAT outputFormat,
              uint32_t renderW, uint32_t renderH,
              uint32_t displayW, uint32_t displayH);
    void Execute(ID3D12GraphicsCommandList* cmdList, const SGSR2Params& params);
    void Shutdown();
    bool Resize(uint32_t renderW, uint32_t renderH,
                uint32_t displayW, uint32_t displayH);
    bool IsInitialized() const { return m_initialized; }

private:
    bool CreateReprojectPipeline();
    bool CreateUpscalePipeline();
    bool CreateHistoryResource();

    void Transition(ID3D12GraphicsCommandList* cl, ID3D12Resource* res,
                    D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after);

    ID3D12Device*           m_device       = nullptr;
    ID3D12RootSignature*    m_reprojectSig = nullptr;
    ID3D12PipelineState*    m_reprojectPSO = nullptr;
    ID3D12RootSignature*    m_upscaleSig   = nullptr;
    ID3D12PipelineState*    m_upscalePSO   = nullptr;
    ID3D12DescriptorHeap*   m_srvHeap      = nullptr;
    ID3D12DescriptorHeap*   m_samplerHeap  = nullptr;
    ID3D12Resource*         m_historyTex   = nullptr;  // Previous frame accumulation
    ID3D12Resource*         m_tempTex      = nullptr;  // Intermediate reproject result

    uint32_t m_renderW = 0, m_renderH = 0;
    uint32_t m_displayW = 0, m_displayH = 0;
    DXGI_FORMAT m_format = DXGI_FORMAT_UNKNOWN;
    bool m_initialized = false;
    bool m_historyValid = false;   // First frame has no history
};

} // namespace adrena