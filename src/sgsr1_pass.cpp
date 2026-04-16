#include "sgsr1_pass.h"
#include "config.h"
#include "logger.h"
#include "embedded_shaders.h"
#include <d3dcompiler.h>

namespace adrena {

struct SGSRConstants {
    uint32_t renderSize[2];
    uint32_t displaySize[2];
    float    sharpness;
    uint32_t frameCount;
};

SGSR1Pass::SGSR1Pass() {}
SGSR1Pass::~SGSR1Pass()
{
    if (m_easuCS11) m_easuCS11->Release();
    if (m_rcasCS11) m_rcasCS11->Release();
    if (m_constBuf11) m_constBuf11->Release();
    if (m_sampler11) m_sampler11->Release();
    if (m_rootSig12) m_rootSig12->Release();
    if (m_easuPSO12) m_easuPSO12->Release();
    if (m_rcasPSO12) m_rcasPSO12->Release();
    if (m_constBuf12) m_constBuf12->Release();
}

// ─── D3D11 Init ──────────────────────────────────────
bool SGSR1Pass::Init11(ID3D11Device* device, UINT rw, UINT rh, UINT dw, UINT dh, DXGI_FORMAT fmt)
{
    m_dev11 = device;
    m_renderW = rw; m_renderH = rh;
    m_displayW = dw; m_displayH = dh;
    device->AddRef();

    if (!CreateShaders11()) return false;

    // Constant buffer
    D3D11_BUFFER_DESC bd = {};
    bd.ByteWidth = sizeof(SGSRConstants);
    bd.Usage = D3D11_USAGE_DYNAMIC;
    bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    device->CreateBuffer(&bd, nullptr, &m_constBuf11);

    // Sampler
    D3D11_SAMPLER_DESC sd = {};
    sd.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    sd.AddressU = sd.AddressV = sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    sd.MaxAnisotropy = 1;
    sd.ComparisonFunc = D3D11_COMPARISON_NEVER;
    sd.MaxLOD = FLT_MAX;
    device->CreateSamplerState(&sd, &m_sampler11);

    AD_LOG_I("SGSR1 D3D11 initialized: %ux%u → %ux%u", rw, rh, dw, dh);
    return true;
}

bool SGSR1Pass::CreateShaders11()
{
    ID3DBlob* easuBlob = nullptr;
    ID3DBlob* rcasBlob = nullptr;
    ID3DBlob* errBlob = nullptr;

    // Compile EASU shader
    HRESULT hr = D3DCompile(
        adrena::shaders::sgsr1_easu,
        adrena::shaders::sgsr1_easu_size,
        "sgsr1_easu.hlsl", nullptr, nullptr,
        "CSMain", "cs_5_0", 0, 0, &easuBlob, &errBlob);

    if (FAILED(hr)) {
        AD_LOG_E("EASU shader compile failed: %s", errBlob ? (char*)errBlob->GetBufferPointer() : "unknown");
        if (errBlob) errBlob->Release();
        return false;
    }

    m_dev11->CreateComputeShader(easuBlob->GetBufferPointer(), easuBlob->GetBufferSize(),
                                  nullptr, &m_easuCS11);
    easuBlob->Release();

    // Compile RCAS shader
    hr = D3DCompile(
        adrena::shaders::sgsr1_rcas,
        adrena::shaders::sgsr1_rcas_size,
        "sgsr1_rcas.hlsl", nullptr, nullptr,
        "CSMain", "cs_5_0", 0, 0, &rcasBlob, &errBlob);

    if (FAILED(hr)) {
        AD_LOG_E("RCAS shader compile failed: %s", errBlob ? (char*)errBlob->GetBufferPointer() : "unknown");
        if (errBlob) errBlob->Release();
        return false;
    }

    m_dev11->CreateComputeShader(rcasBlob->GetBufferPointer(), rcasBlob->GetBufferSize(),
                                  nullptr, &m_rcasCS11);
    rcasBlob->Release();

    AD_LOG_I("SGSR1 D3D11 shaders compiled successfully");
    return true;
}

void SGSR1Pass::Dispatch11(ID3D11DeviceContext* ctx, ID3D11ShaderResourceView* inputSRV, ID3D11UnorderedAccessView* outputUAV)
{
    if (!m_easuCS11 || !ctx) return;

    // Update constants
    D3D11_MAPPED_SUBRESOURCE ms = {};
    ctx->Map(m_constBuf11, 0, D3D11_MAP_WRITE_DISCARD, 0, &ms);
    SGSRConstants* c = (SGSRConstants*)ms.pData;
    c->renderSize[0] = m_renderW;
    c->renderSize[1] = m_renderH;
    c->displaySize[0] = m_displayW;
    c->displaySize[1] = m_displayH;
    c->sharpness = m_sharpness;
    c->frameCount = 0;
    ctx->Unmap(m_constBuf11, 0);

    // EASU pass
    ctx->CSSetShader(m_easuCS11, nullptr, 0);
    ctx->CSSetConstantBuffers(0, 1, &m_constBuf11);
    ctx->CSSetShaderResources(0, 1, &inputSRV);
    ctx->CSSetUnorderedAccessViews(0, 1, &outputUAV, nullptr);
    ctx->CSSetSamplers(0, 1, &m_sampler11);

    UINT gx = (m_displayW + 7) / 8;
    UINT gy = (m_displayH + 7) / 8;
    ctx->Dispatch(gx, gy, 1);

    // Barrier
    ID3D11UnorderedAccessView* nullUAV = nullptr;
    ctx->CSSetUnorderedAccessViews(0, 1, &nullUAV, nullptr);
}

// ─── D3D12 Init ──────────────────────────────────────
bool SGSR1Pass::Init12(ID3D12Device* device, UINT rw, UINT rh, UINT dw, UINT dh, DXGI_FORMAT fmt)
{
    m_dev12 = device;
    m_renderW = rw; m_renderH = rh;
    m_displayW = dw; m_displayH = dh;
    device->AddRef();

    if (!CreateShaders12()) return false;
    if (!CreatePipeline12()) return false;

    // Constant buffer (upload heap)
    D3D12_RESOURCE_DESC rd = {};
    rd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    rd.Width = (sizeof(SGSRConstants) + 255) & ~255; // 256-byte aligned
    rd.Height = 1;
    rd.DepthOrArraySize = 1;
    rd.MipLevels = 1;
    rd.Format = DXGI_FORMAT_UNKNOWN;
    rd.SampleDesc = { 1, 0 };
    rd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

    D3D12_HEAP_PROPERTIES hp = {};
    hp.Type = D3D12_HEAP_TYPE_UPLOAD;

    device->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd,
        D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
        __uuidof(ID3D12Resource), (void**)&m_constBuf12);

    AD_LOG_I("SGSR1 D3D12 initialized: %ux%u → %ux%u", rw, rh, dw, dh);
    return true;
}

bool SGSR1Pass::CreateShaders12()
{
    ID3DBlob* easuBlob = nullptr;
    ID3DBlob* errBlob = nullptr;

    HRESULT hr = D3DCompile(
        adrena::shaders::sgsr1_easu,
        adrena::shaders::sgsr1_easu_size,
        "sgsr1_easu.hlsl", nullptr, nullptr,
        "CSMain", "cs_5_0", 0, 0, &easuBlob, &errBlob);

    if (FAILED(hr)) {
        AD_LOG_E("SGSR1 D3D12 EASU compile failed");
        if (errBlob) errBlob->Release();
        return false;
    }

    // Store shader bytecode for PSO creation
    // (In full impl, we'd store the blob)
    easuBlob->Release();

    AD_LOG_I("SGSR1 D3D12 shaders compiled");
    return true;
}

bool SGSR1Pass::CreatePipeline12()
{
    if (!m_dev12) return false;

    // Root signature: b0 = constants, t0 = input, u0 = output, s0 = sampler
    D3D12_ROOT_PARAMETER params[4] = {};

    // CBV
    params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    params[0].Descriptor.ShaderRegister = 0;
    params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    // SRV
    params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
    params[1].Descriptor.ShaderRegister = 0;
    params[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    // UAV
    params[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
    params[2].Descriptor.ShaderRegister = 0;
    params[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    // Static sampler
    D3D12_STATIC_SAMPLER_DESC staticSampler = {};
    staticSampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    staticSampler.AddressU = staticSampler.AddressV = staticSampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    staticSampler.ShaderRegister = 0;
    staticSampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    D3D12_ROOT_SIGNATURE_DESC rsDesc = {};
    rsDesc.NumParameters = 3;
    rsDesc.pParameters = params;
    rsDesc.NumStaticSamplers = 1;
    rsDesc.pStaticSamplers = &staticSampler;

    ID3DBlob* sigBlob = nullptr;
    ID3DBlob* errBlob = nullptr;
    D3D12SerializeRootSignature(&rsDesc, D3D_ROOT_SIGNATURE_VERSION_1, &sigBlob, &errBlob);
    m_dev12->CreateRootSignature(0, sigBlob->GetBufferPointer(), sigBlob->GetBufferSize(),
                                  __uuidof(ID3D12RootSignature), (void**)&m_rootSig12);
    sigBlob->Release();
    if (errBlob) errBlob->Release();

    AD_LOG_I("SGSR1 D3D12 root signature created");
    return true;
}

void SGSR1Pass::Dispatch12(ID3D12GraphicsCommandList* cmd, D3D12_GPU_DESCRIPTOR_HANDLE inputSRV, D3D12_GPU_DESCRIPTOR_HANDLE outputUAV)
{
    if (!cmd || !m_rootSig12) return;

    cmd->SetComputeRootSignature(m_rootSig12);

    // Set constants
    if (m_constBuf12) {
        void* mapped = nullptr;
        D3D12_RANGE range = { 0, 0 };
        m_constBuf12->Map(0, &range, &mapped);
        SGSRConstants c = { {m_renderW, m_renderH}, {m_displayW, m_displayH}, m_sharpness, 0 };
        memcpy(mapped, &c, sizeof(c));
        m_constBuf12->Unmap(0, nullptr);

        cmd->SetComputeRootConstantBufferView(0, m_constBuf12->GetGPUVirtualAddress());
    }

    UINT gx = (m_displayW + 7) / 8;
    UINT gy = (m_displayH + 7) / 8;
    // cmd->Dispatch(gx, gy, 1); // Would need PSO set first
}

} // namespace adrena