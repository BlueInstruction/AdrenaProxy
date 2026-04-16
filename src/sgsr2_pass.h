#pragma once
#include <d3d11.h>
#include <d3d12.h>
#include <cstdint>

namespace adrena {

class SGSR2Pass {
public:
    SGSR2Pass();
    ~SGSR2Pass();

    bool Init11(ID3D11Device* dev, UINT rw, UINT rh, UINT dw, UINT dh, DXGI_FORMAT fmt);
    void Dispatch11(ID3D11DeviceContext* ctx,
        ID3D11ShaderResourceView* colorSRV,
        ID3D11ShaderResourceView* depthSRV,
        ID3D11ShaderResourceView* motionSRV,
        ID3D11UnorderedAccessView* outputUAV);

    bool Init12(ID3D12Device* dev, UINT rw, UINT rh, UINT dw, UINT dh, DXGI_FORMAT fmt);
    void Dispatch12(ID3D12GraphicsCommandList* cmd,
        D3D12_GPU_DESCRIPTOR_HANDLE colorSRV,
        D3D12_GPU_DESCRIPTOR_HANDLE depthSRV,
        D3D12_GPU_DESCRIPTOR_HANDLE motionSRV,
        D3D12_GPU_DESCRIPTOR_HANDLE outputUAV);

    void SetSharpness(float s) { m_sharpness = s; }
    void SetTemporalWeight(float w) { m_temporalWeight = w; }

private:
    ID3D11Device* m_dev11 = nullptr;
    ID3D11ComputeShader* m_reprojectCS11 = nullptr;
    ID3D11ComputeShader* m_upscaleCS11 = nullptr;
    ID3D11Buffer* m_constBuf11 = nullptr;
    ID3D11SamplerState* m_sampler11 = nullptr;
    ID3D11Texture2D* m_historyTex11 = nullptr;

    ID3D12Device* m_dev12 = nullptr;
    ID3D12RootSignature* m_rootSig12 = nullptr;
    ID3D12PipelineState* m_reprojectPSO12 = nullptr;
    ID3D12PipelineState* m_upscalePSO12 = nullptr;
    ID3D12Resource* m_constBuf12 = nullptr;
    ID3D12Resource* m_historyTex12 = nullptr;

    UINT m_renderW = 0, m_renderH = 0;
    UINT m_displayW = 0, m_displayH = 0;
    float m_sharpness = 0.80f;
    float m_temporalWeight = 0.1f;
    uint32_t m_frameCount = 0;
};

} // namespace adrena