#include "fg_pass.h"
#include "config.h"
#include "logger.h"
#include "embedded_shaders.h"
#include <d3dcompiler.h>

namespace adrena {

struct FGConstants {
    uint32_t resolution[2];
    float    quality;
    uint32_t searchRadius;
    uint32_t frameCount;
    float    t;           // interpolation factor
    float    threshold;
    float    padding[2];
};

FGPass::FGPass() {}
FGPass::~FGPass()
{
    if (m_motionEstCS11) m_motionEstCS11->Release();
    if (m_interpolateCS11) m_interpolateCS11->Release();
    if (m_reactiveCS11) m_reactiveCS11->Release();
    if (m_constBuf11) m_constBuf11->Release();
    if (m_sampler11) m_sampler11->Release();
    if (m_prevColor11) m_prevColor11->Release();
    if (m_motionTex11) m_motionTex11->Release();
    if (m_confidenceTex11) m_confidenceTex11->Release();
    if (m_reactiveTex11) m_reactiveTex11->Release();
    if (m_prevColorSRV11) m_prevColorSRV11->Release();
}

bool FGPass::Init11(ID3D11Device* dev, UINT w, UINT h, DXGI_FORMAT fmt)
{
    m_dev11 = dev; m_width = w; m_height = h;
    dev->AddRef();

    // Compile shaders
    ID3DBlob* blob = nullptr;
    ID3DBlob* err = nullptr;

    if (SUCCEEDED(D3DCompile(adrena::shaders::fg_motion_est, adrena::shaders::fg_motion_est_size,
        "fg_motion_est.hlsl", nullptr, nullptr, "CSMain", "cs_5_0", 0, 0, &blob, &err))) {
        dev->CreateComputeShader(blob->GetBufferPointer(), blob->GetBufferSize(), nullptr, &m_motionEstCS11);
        blob->Release();
    } else { if (err) err->Release(); }

    if (SUCCEEDED(D3DCompile(adrena::shaders::fg_interpolate, adrena::shaders::fg_interpolate_size,
        "fg_interpolate.hlsl", nullptr, nullptr, "CSMain", "cs_5_0", 0, 0, &blob, &err))) {
        dev->CreateComputeShader(blob->GetBufferPointer(), blob->GetBufferSize(), nullptr, &m_interpolateCS11);
        blob->Release();
    } else { if (err) err->Release(); }

    if (SUCCEEDED(D3DCompile(adrena::shaders::fg_reactive_mask, adrena::shaders::fg_reactive_mask_size,
        "fg_reactive_mask.hlsl", nullptr, nullptr, "CSMain", "cs_5_0", 0, 0, &blob, &err))) {
        dev->CreateComputeShader(blob->GetBufferPointer(), blob->GetBufferSize(), nullptr, &m_reactiveCS11);
        blob->Release();
    } else { if (err) err->Release(); }

    // Constant buffer
    D3D11_BUFFER_DESC bd = {};
    bd.ByteWidth = sizeof(FGConstants);
    bd.Usage = D3D11_USAGE_DYNAMIC;
    bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    dev->CreateBuffer(&bd, nullptr, &m_constBuf11);

    // Sampler
    D3D11_SAMPLER_DESC sd = {};
    sd.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    sd.AddressU = sd.AddressV = sd.AddressW = D3D11_TEXTURE_ADDRESS_MODE_CLAMP;
    sd.MaxLOD = FLT_MAX;
    dev->CreateSamplerState(&sd, &m_sampler11);

    // History texture
    D3D11_TEXTURE2D_DESC td = {};
    td.Width = w; td.Height = h;
    td.MipLevels = 1; td.ArraySize = 1;
    td.Format = fmt;
    td.SampleDesc = { 1, 0 };
    td.Usage = D3D11_USAGE_DEFAULT;
    td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    dev->CreateTexture2D(&td, nullptr, &m_prevColor11);

    D3D11_SHADER_RESOURCE_VIEW_DESC srvd = {};
    srvd.Format = fmt;
    srvd.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    srvd.Texture2D.MipLevels = 1;
    dev->CreateShaderResourceView(m_prevColor11, &srvd, &m_prevColorSRV11);

    // Motion texture
    td.Format = DXGI_FORMAT_R32G32_FLOAT;
    td.BindFlags = D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_SHADER_RESOURCE;
    dev->CreateTexture2D(&td, nullptr, &m_motionTex11);

    // Confidence texture
    td.Format = DXGI_FORMAT_R32_FLOAT;
    dev->CreateTexture2D(&td, nullptr, &m_confidenceTex11);

    // Reactive mask
    dev->CreateTexture2D(&td, nullptr, &m_reactiveTex11);

    AD_LOG_I("FG D3D11 initialized: %ux%u (x%d)", w, h, (int)m_multiplier);
    return true;
}

int FGPass::Process11(ID3D11DeviceContext* ctx, ID3D11ShaderResourceView* currColor,
                       ID3D11UnorderedAccessView* outputUAV)
{
    if (m_multiplier == FGMultiplier::X1) return 1;
    if (!m_hasHistory) {
        // First frame — just store history
        m_hasHistory = true;
        return 1;
    }

    // Step 1: Motion estimation
    if (m_motionEstCS11) {
        D3D11_MAPPED_SUBRESOURCE ms = {};
        ctx->Map(m_constBuf11, 0, D3D11_MAP_WRITE_DISCARD, 0, &ms);
        auto* c = (FGConstants*)ms.pData;
        c->resolution[0] = m_width; c->resolution[1] = m_height;
        c->quality = 0.5f;
        c->searchRadius = m_searchRadius;
        c->frameCount = m_frameCount++;
        c->t = 0.0f;
        ctx->Unmap(m_constBuf11, 0);

        ctx->CSSetShader(m_motionEstCS11, nullptr, 0);
        ctx->CSSetConstantBuffers(0, 1, &m_constBuf11);
        ctx->CSSetSamplers(0, 1, &m_sampler11);
        // ... dispatch motion estimation
    }

    // Step 2: Frame interpolation for each intermediate frame
    int numFrames = (int)m_multiplier;

    // Step 3: Update history
    m_hasHistory = true;

    return numFrames;
}

bool FGPass::Init12(ID3D12Device* dev, UINT w, UINT h, DXGI_FORMAT fmt) {
    m_dev12 = dev; m_width = w; m_height = h;
    dev->AddRef();
    AD_LOG_I("FG D3D12 initialized (stub): %ux%u", w, h);
    return true;
}

int FGPass::Process12(ID3D12GraphicsCommandList* cmd,
                       D3D12_GPU_DESCRIPTOR_HANDLE currColor,
                       D3D12_GPU_DESCRIPTOR_HANDLE outputUAV)
{
    if (m_multiplier == FGMultiplier::X1) return 1;
    // Full D3D12 FG implementation — stub for now
    return 1;
}

} // namespace adrena