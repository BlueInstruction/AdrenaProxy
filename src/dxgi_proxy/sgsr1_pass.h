#pragma once
#include <d3d11.h>
#include <d3d12.h>
#include <cstdint>

namespace adrena {

class SGSR1Pass {
public:
    SGSR1Pass();
    ~SGSR1Pass();

    // D3D11 path
    bool Init11(ID3D11Device* device, UINT renderW, UINT renderH, UINT displayW, UINT displayH, DXGI_FORMAT fmt);
    void Dispatch11(ID3D11DeviceContext* ctx, ID3D11ShaderResourceView* inputSRV, ID3D11UnorderedAccessView* outputUAV);

    // D3D12 path
    bool Init12(ID3D12Device* device, UINT renderW, UINT renderH, UINT displayW, UINT displayH, DXGI_FORMAT fmt);
    void Dispatch12(ID3D12GraphicsCommandList* cmd, D3D12_GPU_DESCRIPTOR_HANDLE inputSRV, D3D12_GPU_DESCRIPTOR_HANDLE outputUAV);

    void SetSharpness(float s) { m_sharpness = s; }

private:
    bool CreateShaders11();
    bool CreateShaders12();
    bool CreatePipeline12();

    // D3D11 resources
    ID3D11Device* m_dev11 = nullptr;
    ID3D11ComputeShader* m_easuCS11 = nullptr;
    ID3D11ComputeShader* m_rcasCS11 = nullptr;
    ID3D11Buffer* m_constBuf11 = nullptr;
    ID3D11SamplerState* m_sampler11 = nullptr;

    // D3D12 resources
    ID3D12Device* m_dev12 = nullptr;
    ID3D12RootSignature* m_rootSig12 = nullptr;
    ID3D12PipelineState* m_easuPSO12 = nullptr;
    ID3D12PipelineState* m_rcasPSO12 = nullptr;
    ID3D12Resource* m_constBuf12 = nullptr;

    UINT m_renderW = 0, m_renderH = 0;
    UINT m_displayW = 0, m_displayH = 0;
    float m_sharpness = 0.80f;
};

} // namespace adrena
