#include "fg_pass.h"
#include "logger.h"
#include "embedded_shaders.h"
#include <d3dcompiler.h>
#include <cfloat>

namespace adrena {

FGPass::FGPass() {}
FGPass::~FGPass() {
    if(m_motionEstCS11) m_motionEstCS11->Release(); if(m_interpolateCS11) m_interpolateCS11->Release();
    if(m_constBuf11) m_constBuf11->Release(); if(m_prevColor11) m_prevColor11->Release(); if(m_prevColorSRV11) m_prevColorSRV11->Release();
}

bool FGPass::Init11(ID3D11Device* dev, UINT w, UINT h, DXGI_FORMAT fmt) {
    m_dev11=dev; m_width=w; m_height=h; dev->AddRef();
    ID3DBlob* blob=nullptr; ID3DBlob* err=nullptr;

    if(SUCCEEDED(D3DCompile(adrena::shaders::fg_motion_est, adrena::shaders::fg_motion_est_size,
        "fg_motion_est.hlsl", nullptr, nullptr, "CSMain", "cs_5_0", 0, 0, &blob, &err))) {
        dev->CreateComputeShader(blob->GetBufferPointer(), blob->GetBufferSize(), nullptr, &m_motionEstCS11); blob->Release();
    } else { if(err) err->Release(); }

    if(SUCCEEDED(D3DCompile(adrena::shaders::fg_interpolate, adrena::shaders::fg_interpolate_size,
        "fg_interpolate.hlsl", nullptr, nullptr, "CSMain", "cs_5_0", 0, 0, &blob, &err))) {
        dev->CreateComputeShader(blob->GetBufferPointer(), blob->GetBufferSize(), nullptr, &m_interpolateCS11); blob->Release();
    } else { if(err) err->Release(); }

    D3D11_BUFFER_DESC bd={}; bd.ByteWidth=64; bd.Usage=D3D11_USAGE_DYNAMIC; bd.BindFlags=D3D11_BIND_CONSTANT_BUFFER; bd.CPUAccessFlags=D3D11_CPU_ACCESS_WRITE;
    dev->CreateBuffer(&bd, nullptr, &m_constBuf11);
    D3D11_TEXTURE2D_DESC td={}; td.Width=w; td.Height=h; td.MipLevels=1; td.ArraySize=1; td.Format=fmt; td.SampleDesc={1,0}; td.Usage=D3D11_USAGE_DEFAULT; td.BindFlags=D3D11_BIND_SHADER_RESOURCE;
    dev->CreateTexture2D(&td, nullptr, &m_prevColor11);
    D3D11_SHADER_RESOURCE_VIEW_DESC srvd={}; srvd.Format=fmt; srvd.ViewDimension=D3D11_SRV_DIMENSION_TEXTURE2D; srvd.Texture2D.MipLevels=1;
    dev->CreateShaderResourceView(m_prevColor11, &srvd, &m_prevColorSRV11);
    return true;
}

int FGPass::Process11(ID3D11DeviceContext* ctx, ID3D11ShaderResourceView* currColor, ID3D11UnorderedAccessView* outputUAV) {
    if(m_multiplier == FGMultiplier::X1) return 1;
    if(!m_hasHistory) { m_hasHistory = true; return 1; }
    return (int)m_multiplier;
}

bool FGPass::Init12(ID3D12Device* dev, UINT w, UINT h, DXGI_FORMAT fmt) { return true; }
int FGPass::Process12(ID3D12GraphicsCommandList* cmd, D3D12_GPU_DESCRIPTOR_HANDLE currColor, D3D12_GPU_DESCRIPTOR_HANDLE outputUAV) { return 1; }

} // namespace adrena
