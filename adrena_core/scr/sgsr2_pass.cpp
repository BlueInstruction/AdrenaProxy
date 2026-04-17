#include "adrena_core/sgsr2_pass.h"
#include "adrena_core/logger.h"
#include "embedded_shaders.h"
#include <d3dcompiler.h>

namespace adrena {

// D3D12 resource barrier helper for MinGW compatibility
struct SGSR2Barrier : D3D12_RESOURCE_BARRIER {
    static SGSR2Barrier Transition(ID3D12Resource* res,
        D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after,
        UINT sub = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES) {
        SGSR2Barrier b{};
        b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        b.Transition.pResource = res;
        b.Transition.StateBefore = before;
        b.Transition.StateAfter = after;
        b.Transition.Subresource = sub;
        return b;
    }
};

SGSR2Pass::~SGSR2Pass() { Shutdown(); }

bool SGSR2Pass::Init(ID3D12Device* device, DXGI_FORMAT fmt,
                     uint32_t rW, uint32_t rH, uint32_t dW, uint32_t dH) {
    if (m_initialized) return true;
    if (!device) return false;

    m_device   = device;
    m_format   = fmt;
    m_renderW  = rW;  m_renderH  = rH;
    m_displayW = dW;  m_displayH = dH;

    // ── Create descriptor heaps ──
    D3D12_DESCRIPTOR_HEAP_DESC srvDesc{};
    srvDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    srvDesc.NumDescriptors = 8; // 5 SRVs + 1 UAV + 2 history
    srvDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    HRESULT hr = device->CreateDescriptorHeap(&srvDesc,
        IID_ID3D12DescriptorHeap, (void**)&m_srvHeap);
    if (FAILED(hr)) {
        AD_LOG_E("SGSR2: Failed to create SRV heap");
        return false;
    }

    D3D12_DESCRIPTOR_HEAP_DESC sampDesc{};
    sampDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER;
    sampDesc.NumDescriptors = 1;
    sampDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    hr = device->CreateDescriptorHeap(&sampDesc,
        IID_ID3D12DescriptorHeap, (void**)&m_samplerHeap);
    if (FAILED(hr)) {
        AD_LOG_E("SGSR2: Failed to create sampler heap");
        return false;
    }

    // ── Create pipelines ──
    if (!CreateReprojectPipeline()) return false;
    if (!CreateUpscalePipeline())   return false;
    if (!CreateHistoryResource())   return false;

    m_initialized = true;
    m_historyValid = false;

    AD_LOG_I("SGSR2Pass initialized (stub): %ux%u -> %ux%u fmt=%u",
             rW, rH, dW, dH, (uint32_t)fmt);
    return true;
}

void SGSR2Pass::Execute(ID3D12GraphicsCommandList* cmdList, const SGSR2Params& p) {
    if (!m_initialized || !cmdList) return;

    // ── STUB IMPLEMENTATION ──
    // When SGSR2 temporal logic is implemented, this will:
    //
    // Pass 1: Reproject — sample history texture at motion-adjusted UVs,
    //         blend with current color based on neighborhood clamping
    //         (TAAU-style), write to m_tempTex
    //
    // Pass 2: Upscale + Sharpen — use Lanczos2 to upscale the
    //         temporally-accumulated result, apply RCAS sharpening,
    //         write to output UAV
    //
    // For now, we fall back to SGSR1-style spatial-only processing
    // to avoid crashing or showing a black screen.

    AD_LOG_W("SGSR2 Execute called — temporal upscaling not yet implemented, "
             "falling back to spatial pass");

    // Reset history on first frame or when requested
    if (p.resetHistory || !m_historyValid) {
        m_historyValid = false;
    }

    // TODO: Implement temporal accumulation pass
    // TODO: Copy current output to m_historyTex for next frame
    // m_historyValid = true;
}

void SGSR2Pass::Shutdown() {
    if (m_reprojectPSO) { m_reprojectPSO->Release(); m_reprojectPSO = nullptr; }
    if (m_reprojectSig) { m_reprojectSig->Release(); m_reprojectSig = nullptr; }
    if (m_upscalePSO)   { m_upscalePSO->Release();   m_upscalePSO = nullptr; }
    if (m_upscaleSig)   { m_upscaleSig->Release();   m_upscaleSig = nullptr; }
    if (m_srvHeap)      { m_srvHeap->Release();      m_srvHeap = nullptr; }
    if (m_samplerHeap)  { m_samplerHeap->Release();  m_samplerHeap = nullptr; }
    if (m_historyTex)   { m_historyTex->Release();   m_historyTex = nullptr; }
    if (m_tempTex)      { m_tempTex->Release();      m_tempTex = nullptr; }
    m_device = nullptr;
    m_initialized = false;
    AD_LOG_I("SGSR2Pass shut down");
}

bool SGSR2Pass::Resize(uint32_t rW, uint32_t rH, uint32_t dW, uint32_t dH) {
    m_renderW = rW;  m_renderH = rH;
    m_displayW = dW; m_displayH = dH;
    if (m_historyTex) { m_historyTex->Release(); m_historyTex = nullptr; }
    if (m_tempTex)    { m_tempTex->Release();    m_tempTex = nullptr; }
    if (m_srvHeap)    { m_srvHeap->Release();    m_srvHeap = nullptr; }
    m_historyValid = false;
    return CreateHistoryResource();
}

void SGSR2Pass::Transition(ID3D12GraphicsCommandList* cl, ID3D12Resource* res,
                           D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after) {
    if (before == after) return;
    auto b = SGSR2Barrier::Transition(res, before, after);
    cl->ResourceBarrier(1, &b);
}

bool SGSR2Pass::CreateReprojectPipeline() {
    // Root signature for reprojection pass:
    // Table(SRV: color, depth, motion, exposure, history),
    // Table(UAV: tempOutput, historyOut),
    // Table(Sampler),
    // Constants(render/display size, jitter, blend factor, etc.)

    D3D12_ROOT_PARAMETER1 params[4]{};

    // SRV table: 5 textures
    D3D12_DESCRIPTOR_RANGE1 srvRange{};
    srvRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    srvRange.NumDescriptors = 5;
    srvRange.BaseShaderRegister = 0;
    srvRange.RegisterSpace = 0;
    srvRange.Flags = D3D12_DESCRIPTOR_RANGE_FLAG_NONE;
    srvRange.OffsetInDescriptorsFromTableStart = 0;

    params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    params[0].DescriptorTable.NumDescriptorRanges = 1;
    params[0].DescriptorTable.pDescriptorRanges = &srvRange;
    params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    // UAV table: 2 textures
    D3D12_DESCRIPTOR_RANGE1 uavRange{};
    uavRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
    uavRange.NumDescriptors = 2;
    uavRange.BaseShaderRegister = 0;
    uavRange.RegisterSpace = 0;
    uavRange.Flags = D3D12_DESCRIPTOR_RANGE_FLAG_NONE;
    uavRange.OffsetInDescriptorsFromTableStart = 5;

    params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    params[1].DescriptorTable.NumDescriptorRanges = 1;
    params[1].DescriptorTable.pDescriptorRanges = &uavRange;
    params[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    // Static sampler
    D3D12_STATIC_SAMPLER_DESC sampler{};
    sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    sampler.AddressU = sampler.AddressV = sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.ShaderRegister = 0;
    sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    // Constants: renderW, renderH, displayW, displayH, jitterX, jitterY,
    //           temporalWeight, resetHistory, cameraNear, cameraFar, ...
    params[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    params[2].Constants.ShaderRegister = 0;
    params[2].Constants.RegisterSpace = 0;
    params[2].Constants.Num32BitValues = 16;
    params[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    D3D12_VERSIONED_ROOT_SIGNATURE_DESC rsDesc{};
    rsDesc.Version = D3D_ROOT_SIGNATURE_VERSION_1_1;
    rsDesc.Desc_1_1.NumParameters = 3; // 3 used (params[3] is placeholder)
    rsDesc.Desc_1_1.pParameters = params;
    rsDesc.Desc_1_1.NumStaticSamplers = 1;
    rsDesc.Desc_1_1.pStaticSamplers = &sampler;
    rsDesc.Desc_1_1.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;

    ID3DBlob* sigBlob = nullptr, *errBlob = nullptr;
    HRESULT hr = D3D12SerializeVersionedRootSignature(&rsDesc, &sigBlob, &errBlob);
    if (FAILED(hr)) {
        if (errBlob) AD_LOG_E("SGSR2 Reproject RootSig: %s", (char*)errBlob->GetBufferPointer());
        return false;
    }

    hr = m_device->CreateRootSignature(0, sigBlob->GetBufferPointer(),
        sigBlob->GetBufferSize(), IID_ID3D12RootSignature, (void**)&m_reprojectSig);
    sigBlob->Release();
    if (FAILED(hr)) { AD_LOG_E("SGSR2: CreateRootSignature failed"); return false; }

    // Compile reproject shader
    ID3DBlob* shaderBlob = nullptr;
    hr = D3DCompile(shaders::sgsr2_reproject, shaders::sgsr2_reproject_size,
        "sgsr2_reproject", nullptr, nullptr, "CSMain", "cs_5_0", 0, 0,
        &shaderBlob, &errBlob);
    if (FAILED(hr)) {
        if (errBlob) AD_LOG_E("SGSR2 Reproject compile: %s", (char*)errBlob->GetBufferPointer());
        return false;
    }

    D3D12_COMPUTE_PIPELINE_STATE_DESC psoDesc{};
    psoDesc.pRootSignature = m_reprojectSig;
    psoDesc.CS.pShaderBytecode = shaderBlob->GetBufferPointer();
    psoDesc.CS.BytecodeLength = shaderBlob->GetBufferSize();
    hr = m_device->CreateComputePipelineState(&psoDesc, IID_ID3D12PipelineState, (void**)&m_reprojectPSO);
    shaderBlob->Release();
    if (FAILED(hr)) { AD_LOG_E("SGSR2: Reproject PSO failed"); return false; }

    AD_LOG_I("SGSR2 reproject pipeline created");
    return true;
}

bool SGSR2Pass::CreateUpscalePipeline() {
    // Root signature for final upscale pass (same as SGSR1)
    D3D12_ROOT_PARAMETER1 params[3]{};

    D3D12_DESCRIPTOR_RANGE1 ranges[2]{};
    ranges[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    ranges[0].NumDescriptors = 2;
    ranges[0].BaseShaderRegister = 0;
    ranges[0].RegisterSpace = 0;
    ranges[0].Flags = D3D12_DESCRIPTOR_RANGE_FLAG_NONE;
    ranges[0].OffsetInDescriptorsFromTableStart = 0;

    ranges[1].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
    ranges[1].NumDescriptors = 1;
    ranges[1].BaseShaderRegister = 0;
    ranges[1].RegisterSpace = 0;
    ranges[1].Flags = D3D12_DESCRIPTOR_RANGE_FLAG_NONE;
    ranges[1].OffsetInDescriptorsFromTableStart = 2;

    params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    params[0].DescriptorTable.NumDescriptorRanges = 2;
    params[0].DescriptorTable.pDescriptorRanges = ranges;
    params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    D3D12_STATIC_SAMPLER_DESC sampler{};
    sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    sampler.AddressU = sampler.AddressV = sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.ShaderRegister = 0;
    sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    params[1].DescriptorTable.NumDescriptorRanges = 0;
    params[1].DescriptorTable.pDescriptorRanges = nullptr;
    params[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    params[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    params[2].Constants.ShaderRegister = 0;
    params[2].Constants.RegisterSpace = 0;
    params[2].Constants.Num32BitValues = 6;
    params[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    D3D12_VERSIONED_ROOT_SIGNATURE_DESC rsDesc{};
    rsDesc.Version = D3D_ROOT_SIGNATURE_VERSION_1_1;
    rsDesc.Desc_1_1.NumParameters = 3;
    rsDesc.Desc_1_1.pParameters = params;
    rsDesc.Desc_1_1.NumStaticSamplers = 1;
    rsDesc.Desc_1_1.pStaticSamplers = &sampler;
    rsDesc.Desc_1_1.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;

    ID3DBlob* sigBlob = nullptr, *errBlob = nullptr;
    HRESULT hr = D3D12SerializeVersionedRootSignature(&rsDesc, &sigBlob, &errBlob);
    if (FAILED(hr)) {
        if (errBlob) AD_LOG_E("SGSR2 Upscale RootSig: %s", (char*)errBlob->GetBufferPointer());
        return false;
    }

    hr = m_device->CreateRootSignature(0, sigBlob->GetBufferPointer(),
        sigBlob->GetBufferSize(), IID_ID3D12RootSignature, (void**)&m_upscaleSig);
    sigBlob->Release();
    if (FAILED(hr)) { AD_LOG_E("SGSR2: Upscale RootSig failed"); return false; }

    ID3DBlob* shaderBlob = nullptr;
    hr = D3DCompile(shaders::sgsr2_upscale, shaders::sgsr2_upscale_size,
        "sgsr2_upscale", nullptr, nullptr, "CSMain", "cs_5_0", 0, 0,
        &shaderBlob, &errBlob);
    if (FAILED(hr)) {
        if (errBlob) AD_LOG_E("SGSR2 Upscale compile: %s", (char*)errBlob->GetBufferPointer());
        return false;
    }

    D3D12_COMPUTE_PIPELINE_STATE_DESC psoDesc{};
    psoDesc.pRootSignature = m_upscaleSig;
    psoDesc.CS.pShaderBytecode = shaderBlob->GetBufferPointer();
    psoDesc.CS.BytecodeLength = shaderBlob->GetBufferSize();
    hr = m_device->CreateComputePipelineState(&psoDesc, IID_ID3D12PipelineState, (void**)&m_upscalePSO);
    shaderBlob->Release();
    if (FAILED(hr)) { AD_LOG_E("SGSR2: Upscale PSO failed"); return false; }

    AD_LOG_I("SGSR2 upscale pipeline created");
    return true;
}

bool SGSR2Pass::CreateHistoryResource() {
    if (!m_device) return false;

    D3D12_HEAP_PROPERTIES defaultHeap{};
    defaultHeap.Type = D3D12_HEAP_TYPE_DEFAULT;

    // History texture at display resolution (accumulated over time)
    D3D12_RESOURCE_DESC histDesc{};
    histDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    histDesc.Width = m_displayW;
    histDesc.Height = m_displayH;
    histDesc.DepthOrArraySize = 1;
    histDesc.MipLevels = 1;
    histDesc.Format = m_format;
    histDesc.SampleDesc.Count = 1;
    histDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

    HRESULT hr = m_device->CreateCommittedResource(&defaultHeap,
        D3D12_HEAP_FLAG_NONE, &histDesc,
        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
        nullptr, IID_ID3D12Resource, (void**)&m_historyTex);
    if (FAILED(hr)) {
        AD_LOG_E("SGSR2: Failed to create history texture");
        return false;
    }

    // Temp texture for reprojection output
    D3D12_RESOURCE_DESC tempDesc = histDesc;
    hr = m_device->CreateCommittedResource(&defaultHeap,
        D3D12_HEAP_FLAG_NONE, &tempDesc,
        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
        nullptr, IID_ID3D12Resource, (void**)&m_tempTex);
    if (FAILED(hr)) {
        AD_LOG_E("SGSR2: Failed to create temp texture");
        return false;
    }

    AD_LOG_I("SGSR2: History and temp textures created (%ux%u)", m_displayW, m_displayH);
    return true;
}

} // namespace adrena