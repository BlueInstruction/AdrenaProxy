#include "sgsr1_pass.h"
#include "config.h"
#include "logger.h"
#include "embedded_shaders.h"
#include <d3dcompiler.h>
#include <cfloat>   // FIX: for FLT_MAX

namespace adrena {

struct SGSRConstants { uint32_t renderSize[2]; uint32_t displaySize[2]; float sharpness; uint32_t frameCount; };

SGSR1Pass::SGSR1Pass() {}
SGSR1Pass::~SGSR1Pass() {
    if (m_easuCS11) m_easuCS11->Release(); if (m_rcasCS11) m_rcasCS11->Release();
    if (m_constBuf11) m_constBuf11->Release(); if (m_sampler11) m_sampler11->Release();
    if (m_rootSig12) m_rootSig12->Release(); if (m_easuPSO12) m_easuPSO12->Release();
    if (m_constBuf12) m_constBuf12->Release();
    if (m_dev11) m_dev11->Release(); if (m_dev12) m_dev12->Release();
}

bool SGSR1Pass::Init11(ID3D11Device* device, UINT rw, UINT rh, UINT dw, UINT dh, DXGI_FORMAT fmt) {
    m_dev11 = device; m_renderW = rw; m_renderH = rh; m_displayW = dw; m_displayH = dh; device->AddRef();
    ID3DBlob* blob = nullptr; ID3DBlob* err = nullptr;

    // FIX: Use correct variable names (without _hlsl suffix since we fixed embed_shaders.cmake)
    if (SUCCEEDED(D3DCompile(adrena::shaders::sgsr1_easu, adrena::shaders::sgsr1_easu_size,
        "sgsr1_easu.hlsl", nullptr, nullptr, "CSMain", "cs_5_0", 0, 0, &blob, &err))) {
        device->CreateComputeShader(blob->GetBufferPointer(), blob->GetBufferSize(), nullptr, &m_easuCS11); blob->Release();
    } else { if(err) err->Release(); }

    if (SUCCEEDED(D3DCompile(adrena::shaders::sgsr1_rcas, adrena::shaders::sgsr1_rcas_size,
        "sgsr1_rcas.hlsl", nullptr, nullptr, "CSMain", "cs_5_0", 0, 0, &blob, &err))) {
        device->CreateComputeShader(blob->GetBufferPointer(), blob->GetBufferSize(), nullptr, &m_rcasCS11); blob->Release();
    } else { if(err) err->Release(); }

    D3D11_BUFFER_DESC bd = {}; bd.ByteWidth = sizeof(SGSRConstants); bd.Usage = D3D11_USAGE_DYNAMIC;
    bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER; bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    device->CreateBuffer(&bd, nullptr, &m_constBuf11);

    D3D11_SAMPLER_DESC sd = {}; sd.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    sd.AddressU = sd.AddressV = sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP; sd.MaxLOD = FLT_MAX;
    device->CreateSamplerState(&sd, &m_sampler11);
    return true;
}

void SGSR1Pass::Dispatch11(ID3D11DeviceContext* ctx, ID3D11ShaderResourceView* inputSRV, ID3D11UnorderedAccessView* outputUAV) {
    if (!m_easuCS11 || !ctx) return;
    D3D11_MAPPED_SUBRESOURCE ms = {}; ctx->Map(m_constBuf11, 0, D3D11_MAP_WRITE_DISCARD, 0, &ms);
    SGSRConstants* c = (SGSRConstants*)ms.pData;
    c->renderSize[0]=m_renderW; c->renderSize[1]=m_renderH; c->displaySize[0]=m_displayW; c->displaySize[1]=m_displayH;
    c->sharpness=m_sharpness; c->frameCount=0;
    ctx->Unmap(m_constBuf11, 0);
    ctx->CSSetShader(m_easuCS11, nullptr, 0); ctx->CSSetConstantBuffers(0, 1, &m_constBuf11);
    ctx->CSSetShaderResources(0, 1, &inputSRV); ctx->CSSetUnorderedAccessViews(0, 1, &outputUAV, nullptr);
    ctx->CSSetSamplers(0, 1, &m_sampler11);
    ctx->Dispatch((m_displayW+7)/8, (m_displayH+7)/8, 1);
    ID3D11UnorderedAccessView* nullUAV = nullptr; ctx->CSSetUnorderedAccessViews(0, 1, &nullUAV, nullptr);
}

bool SGSR1Pass::Init12(ID3D12Device* device, UINT rw, UINT rh, UINT dw, UINT dh, DXGI_FORMAT fmt) {
    m_dev12 = device; m_renderW = rw; m_renderH = rh; m_displayW = dw; m_displayH = dh; device->AddRef(); return true;
}

void SGSR1Pass::Dispatch12(ID3D12GraphicsCommandList* cmd, D3D12_GPU_DESCRIPTOR_HANDLE inputSRV, D3D12_GPU_DESCRIPTOR_HANDLE outputUAV) {
    // Full D3D12 SGSR1 implementation — stub
}

} // namespace adrena
