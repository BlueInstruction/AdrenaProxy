#pragma once
#include <d3d12.h>
#include <dxgi1_4.h>
#include <cstdint>

namespace adrena {

enum class SGSROperationMode : uint32_t {
    RGBA = 1,
    RGBY = 3,
    LERP = 4
};

struct SGSRParams {
    ID3D12Resource* color    = nullptr;
    ID3D12Resource* depth    = nullptr;
    ID3D12Resource* motion   = nullptr;
    ID3D12Resource* output   = nullptr;

    float    sharpness       = 2.0f;
    uint32_t renderWidth     = 0;
    uint32_t renderHeight    = 0;
    uint32_t displayWidth    = 0;
    uint32_t displayHeight   = 0;

    SGSROperationMode operationMode    = SGSROperationMode::RGBA;
    float             edgeThreshold    = 8.0f / 255.0f;
    bool              useEdgeDirection = false;
};

class SGSR1Pass {
public:
    SGSR1Pass() = default;
    ~SGSR1Pass();

    SGSR1Pass(const SGSR1Pass&) = delete;
    SGSR1Pass& operator=(const SGSR1Pass&) = delete;

    bool Init(ID3D12Device* device, DXGI_FORMAT outputFormat,
              uint32_t renderW, uint32_t renderH,
              uint32_t displayW, uint32_t displayH);
    void Execute(ID3D12GraphicsCommandList* cmdList, const SGSRParams& params);
    void Shutdown();
    bool Resize(uint32_t renderW, uint32_t renderH,
                uint32_t displayW, uint32_t displayH);
    bool IsInitialized() const { return m_initialized; }

private:
    bool CreatePipeline();
    bool CreateIntermediate();
    void Transition(ID3D12GraphicsCommandList* cl, ID3D12Resource* res,
                    D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after);

    ID3D12Device*           m_device        = nullptr;
    ID3D12RootSignature*    m_rootSig       = nullptr;
    ID3D12PipelineState*    m_pso           = nullptr;
    ID3D12DescriptorHeap*   m_srvHeap       = nullptr;
    ID3D12DescriptorHeap*   m_samplerHeap   = nullptr;
    ID3D12Resource*         m_intermediate  = nullptr;

    DXGI_FORMAT m_format     = DXGI_FORMAT_UNKNOWN;
    uint32_t    m_renderW    = 0;
    uint32_t    m_renderH    = 0;
    uint32_t    m_displayW   = 0;
    uint32_t    m_displayH   = 0;
    UINT        m_srvIncSize = 0;
    bool        m_initialized = false;
};

} // namespace adrena
