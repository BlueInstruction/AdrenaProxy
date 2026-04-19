#pragma once
#include <d3d12.h>
#include <dxgi1_4.h>
#include <cstdint>

namespace adrena {

// SGSR2 Parameters matching the official Qualcomm 2-pass variant
struct SGSR2Params {
    ID3D12Resource* color       = nullptr;
    ID3D12Resource* depth       = nullptr;
    ID3D12Resource* motion      = nullptr;
    ID3D12Resource* exposure    = nullptr;
    ID3D12Resource* output      = nullptr;

    float    deltaTime          = 0.0f;
    uint32_t renderWidth        = 0;
    uint32_t renderHeight       = 0;
    uint32_t displayWidth       = 0;
    uint32_t displayHeight      = 0;
    float    sharpness          = 0.80f;
    bool     resetHistory       = false;

    float    jitterX            = 0.0f;
    float    jitterY            = 0.0f;
    float    preExposure        = 1.0f;
    float    cameraNear         = 0.01f;
    float    cameraFar          = 1000.0f;
    float    cameraFovAngleHor  = 1.0472f;
    float    minLerpContribution= 0.15f;
    bool     bSameCamera        = true;
    int32_t  motionVectorScale  = 1;

    // clipToPrevClip matrix (4x4 row-major as 4 float4)
    float    clipToPrevClip[4][4] = {};
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
    bool CreateConvertPipeline();
    bool CreateUpscalePipeline();
    bool CreateIntermediateResources();
    void Transition(ID3D12GraphicsCommandList* cl, ID3D12Resource* res,
                    D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after);

    ID3D12Device*           m_device       = nullptr;
    ID3D12RootSignature*    m_convertSig   = nullptr;
    ID3D12PipelineState*    m_convertPSO   = nullptr;
    ID3D12RootSignature*    m_upscaleSig   = nullptr;
    ID3D12PipelineState*    m_upscalePSO   = nullptr;
    ID3D12DescriptorHeap*   m_srvHeap      = nullptr;
    ID3D12DescriptorHeap*   m_samplerHeap  = nullptr;

    // Intermediate resources from convert pass
    ID3D12Resource*         m_motionDepthClip = nullptr;  // RGBA16F
    ID3D12Resource*         m_yCocgColor      = nullptr;  // R32UI

    // History textures (swapped each frame)
    ID3D12Resource*         m_history[2]   = {nullptr, nullptr};
    int                     m_historyIdx   = 0;

    uint32_t m_renderW = 0, m_renderH = 0;
    uint32_t m_displayW = 0, m_displayH = 0;
    DXGI_FORMAT m_format = DXGI_FORMAT_UNKNOWN;
    bool m_initialized = false;
    bool m_historyValid = false;
};

} // namespace adrena
