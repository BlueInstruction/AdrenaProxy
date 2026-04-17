#pragma once
#include <d3d11.h>
#include <d3d12.h>
#include <cstdint>

namespace adrena {

enum class FGMultiplier { X1 = 1, X2 = 2, X3 = 3, X4 = 4 };

class FGPass {
public:
    FGPass();
    ~FGPass();

    bool Init11(ID3D11Device* dev, UINT w, UINT h, DXGI_FORMAT fmt);
    bool Init12(ID3D12Device* dev, UINT w, UINT h, DXGI_FORMAT fmt);

    // Called every real frame — returns number of frames to present
    int Process11(ID3D11DeviceContext* ctx, ID3D11ShaderResourceView* currColor,
                  ID3D11UnorderedAccessView* outputUAV);

    int Process12(ID3D12GraphicsCommandList* cmd,
                  D3D12_GPU_DESCRIPTOR_HANDLE currColor,
                  D3D12_GPU_DESCRIPTOR_HANDLE outputUAV);

    void SetMultiplier(FGMultiplier m) { m_multiplier = m; }
    void SetMotionSearchRadius(int r) { m_searchRadius = r; }
    FGMultiplier GetMultiplier() const { return m_multiplier; }

private:
    ID3D11Device* m_dev11 = nullptr;
    ID3D11ComputeShader* m_motionEstCS11 = nullptr;
    ID3D11ComputeShader* m_interpolateCS11 = nullptr;
    ID3D11ComputeShader* m_reactiveCS11 = nullptr;
    ID3D11Buffer* m_constBuf11 = nullptr;
    ID3D11SamplerState* m_sampler11 = nullptr;

    // History buffers
    ID3D11Texture2D* m_prevColor11 = nullptr;
    ID3D11Texture2D* m_motionTex11 = nullptr;
    ID3D11Texture2D* m_confidenceTex11 = nullptr;
    ID3D11Texture2D* m_reactiveTex11 = nullptr;
    ID3D11ShaderResourceView* m_prevColorSRV11 = nullptr;

    ID3D12Device* m_dev12 = nullptr;

    UINT m_width = 0, m_height = 0;
    FGMultiplier m_multiplier = FGMultiplier::X1;
    int m_searchRadius = 8;
    uint32_t m_frameCount = 0;
    bool m_hasHistory = false;
};

} // namespace adrena
