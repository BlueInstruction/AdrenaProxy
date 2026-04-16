#include "sgsr2_pass.h"
#include "logger.h"
#include "embedded_shaders.h"
#include <d3dcompiler.h>

namespace adrena {

struct SGSR2Constants {
    uint32_t renderSize[2];
    uint32_t displaySize[2];
    float    sharpness;
    uint32_t frameCount;
    float    jitter[2];
    float    temporalWeight;
};

SGSR2Pass::SGSR2Pass() {}
SGSR2Pass::~SGSR2Pass()
{
    if (m_reprojectCS11) m_reprojectCS11->Release();
    if (m_upscaleCS11) m_upscaleCS11->Release();
    if (m_constBuf11) m_constBuf11->Release();
    if (m_sampler11) m_sampler11->Release();
    if (m_historyTex11) m_historyTex11->Release();
    if (m_rootSig12) m_rootSig12->Release();
    if (m_reprojectPSO12) m_reprojectPSO12->Release();
    if (m_upscalePSO12) m_upscalePSO12->Release();
    if (m_constBuf12) m_constBuf12->Release();
    if (m_historyTex12) m_historyTex12->Release();
}

bool SGSR2Pass::Init11(ID3D11Device* dev, UINT rw, UINT rh, UINT dw, UINT dh, DXGI_FORMAT fmt)
{
    m_dev11 = dev;
    m_renderW = rw; m_renderH = rh;
    m_displayW = dw; m_displayH = dh;
    dev->AddRef();

    // Compile reproject shader
    ID3DBlob* blob = nullptr;
    ID3DBlob* err = nullptr;
    HRESULT hr = D3DCompile(adrena::shaders::sgsr2_reproject, adrena::shaders::sgsr2_reproject_size,
        "sgsr2_reproject.hlsl", nullptr, nullptr, "CSMain", "cs_5_0", 0, 0, &blob, &err);
    if (SUCCEEDED(hr)) {
        dev->CreateComputeShader(blob->GetBufferPointer(), blob->GetBufferSize(), nullptr, &m_reprojectCS11);
        blob->Release();
    } else {
        AD_LOG_E("SGSR2 reproject compile failed");
        if (err) err->Release();
    }

    // Compile upscale shader
    hr = D3DCompile(adrena::shaders::sgsr2_upscale, adrena::shaders::sgsr2_upscale_size,
        "sgsr2_upscale.hlsl", nullptr, nullptr, "CSMain", "cs_5_0", 0, 0, &blob, &err);
    if (SUCCEEDED(hr)) {
        dev->CreateComputeShader(blob->GetBufferPointer(), blob->GetBufferSize(), nullptr, &m_upscaleCS11);
        blob->Release();
    } else {
        AD_LOG_E("SGSR2 upscale compile failed");
        if (err) err->Release();
    }

    // Constant buffer
    D3D11_BUFFER_DESC bd = {};
    bd.ByteWidth = sizeof(SGSR2Constants);
    bd.Usage = D3D11_USAGE_DYNAMIC;
    bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    dev->CreateBuffer(&bd, nullptr, &m_constBuf11);

    // History texture
    D3D11_TEXTURE2D_DESC ht = {};
    ht.Width = dw; ht.Height = dh;
    ht.MipLevels = 1; ht.ArraySize = 1;
    ht.Format = fmt;
    ht.SampleDesc = { 1, 0 };
    ht.Usage = D3D11_USAGE_DEFAULT;
    ht.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
    dev->CreateTexture2D(&ht, nullptr, &m_historyTex11);

    AD_LOG_I("SGSR2 D3D11 initialized: %ux%u → %ux%u", rw, rh, dw, dh);
    return true;
}

void SGSR2Pass::Dispatch11(ID3D11DeviceContext* ctx,
    ID3D11ShaderResourceView* colorSRV,
    ID3D11ShaderResourceView* depthSRV,
    ID3D11ShaderResourceView* motionSRV,
    ID3D11UnorderedAccessView* outputUAV)
{
    if (!m_reprojectCS11 || !ctx) return;

    // Update constants
    D3D11_MAPPED_SUBRESOURCE ms = {};
    ctx->Map(m_constBuf11, 0, D3D11_MAP_WRITE_DISCARD, 0, &ms);
    auto* c = (SGSR2Constants*)ms.pData;
    c->renderSize[0] = m_renderW; c->renderSize[1] = m_renderH;
    c->displaySize[0] = m_displayW; c->displaySize[1] = m_displayH;
    c->sharpness = m_sharpness;
    c->frameCount = m_frameCount++;
    c->jitter[0] = 0.0f; c->jitter[1] = 0.0f;
    c->temporalWeight = m_temporalWeight;
    ctx->Unmap(m_constBuf11, 0);

    // Reproject pass
    ctx->CSSetShader(m_reprojectCS11, nullptr, 0);
    ctx->CSSetConstantBuffers(0, 1, &m_constBuf11);
    ID3D11ShaderResourceView* srvs[] = { colorSRV, depthSRV, motionSRV };
    ctx->CSSetShaderResources(0, 3, srvs);
    ctx->CSSetUnorderedAccessViews(0, 1, &outputUAV, nullptr);

    UINT gx = (m_displayW + 7) / 8;
    UINT gy = (m_displayH + 7) / 8;
    ctx->Dispatch(gx, gy, 1);

    ID3D11UnorderedAccessView* nullUAV = nullptr;
    ctx->CSSetUnorderedAccessViews(0, 1, &nullUAV, nullptr);
}

bool SGSR2Pass::Init12(ID3D12Device* dev, UINT rw, UINT rh, UINT dw, UINT dh, DXGI_FORMAT fmt) {
    m_dev12 = dev; m_renderW = rw; m_renderH = rh; m_displayW = dw; m_displayH = dh;
    dev->AddRef();
    AD_LOG_I("SGSR2 D3D12 initialized (stub): %ux%u → %ux%u", rw, rh, dw, dh);
    return true;
}

void SGSR2Pass::Dispatch12(ID3D12GraphicsCommandList* cmd,
    D3D12_GPU_DESCRIPTOR_HANDLE colorSRV, D3D12_GPU_DESCRIPTOR_HANDLE depthSRV,
    D3D12_GPU_DESCRIPTOR_HANDLE motionSRV, D3D12_GPU_DESCRIPTOR_HANDLE outputUAV)
{
    // Full D3D12 SGSR2 implementation — stub for now
}

} // namespace adrena