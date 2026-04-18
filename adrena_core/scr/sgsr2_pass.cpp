#include "adrena_core/sgsr2_pass.h"
#include "adrena_core/logger.h"
#include "embedded_shaders.h"
#include <d3dcompiler.h>

namespace adrena {

struct SGSR2Barrier : D3D12_RESOURCE_BARRIER {
    static SGSR2Barrier Transition(ID3D12Resource* res, D3D12_RESOURCE_STATES before,
        D3D12_RESOURCE_STATES after, UINT sub = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES) {
        SGSR2Barrier b{}; b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        b.Transition.pResource = res; b.Transition.StateBefore = before;
        b.Transition.StateAfter = after; b.Transition.Subresource = sub; return b;
    }
};

SGSR2Pass::~SGSR2Pass() { Shutdown(); }

bool SGSR2Pass::Init(ID3D12Device* device, DXGI_FORMAT fmt,
                     uint32_t rW, uint32_t rH, uint32_t dW, uint32_t dH) {
    if (m_initialized) return true;
    if (!device) return false;

    m_device = device; m_format = fmt;
    m_renderW = rW; m_renderH = rH;
    m_displayW = dW; m_displayH = dH;

    // Create descriptor heaps
    D3D12_DESCRIPTOR_HEAP_DESC srvDesc{};
    srvDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    srvDesc.NumDescriptors = 10;
    srvDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    HRESULT hr = device->CreateDescriptorHeap(&srvDesc,
        IID_ID3D12DescriptorHeap, (void**)&m_srvHeap);
    if (FAILED(hr)) return false;

    D3D12_DESCRIPTOR_HEAP_DESC sampDesc{};
    sampDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER;
    sampDesc.NumDescriptors = 1;
    sampDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    hr = device->CreateDescriptorHeap(&sampDesc,
        IID_ID3D12DescriptorHeap, (void**)&m_samplerHeap);
    if (FAILED(hr)) return false;

    if (!CreateConvertPipeline()) return false;
    if (!CreateUpscalePipeline()) return false;
    if (!CreateIntermediateResources()) return false;

    m_initialized = true;
    m_historyValid = false;
    AD_LOG_I("SGSR2Pass initialized (official v2 2-pass): %ux%u -> %ux%u", rW, rH, dW, dH);
    return true;
}

void SGSR2Pass::Execute(ID3D12GraphicsCommandList* cl, const SGSR2Params& p) {
    if (!m_initialized || !cl) return;

    // ── Pass 1: Convert ──
    cl->SetComputeRootSignature(m_convertSig);
    cl->SetPipelineState(m_convertPSO);
    ID3D12DescriptorHeap* heaps[] = { m_srvHeap, m_samplerHeap };
    cl->SetDescriptorHeaps(2, heaps);
    cl->SetComputeRootDescriptorTable(0, m_srvHeap->GetGPUDescriptorHandleForHeapStart());

    // Set root constants for convert pass
    struct ConvertConstants {
        uint32_t renderW, renderH, displayW, displayH;
        float renderRcpX, renderRcpY, displayRcpX, displayRcpY;
        float jitterX, jitterY;
        float clipToPrevClip[4][4];
        float preExposure, cameraFovAngleHor, cameraNear, minLerpContrib;
        uint32_t bSameCamera, reset;
    } c{};
    c.renderW = p.renderWidth; c.renderH = p.renderHeight;
    c.displayW = p.displayWidth; c.displayH = p.displayHeight;
    c.renderRcpX = 1.0f / p.renderWidth; c.renderRcpY = 1.0f / p.renderHeight;
    c.displayRcpX = 1.0f / p.displayWidth; c.displayRcpY = 1.0f / p.displayHeight;
    c.jitterX = p.jitterX; c.jitterY = p.jitterY;
    memcpy(c.clipToPrevClip, p.clipToPrevClip, sizeof(float) * 16);
    c.preExposure = p.preExposure;
    c.cameraFovAngleHor = p.cameraFovAngleHor;
    c.cameraNear = p.cameraNear;
    c.minLerpContrib = p.minLerpContribution;
    c.bSameCamera = p.bSameCamera ? 1 : 0;
    c.reset = p.resetHistory ? 1 : 0;

    cl->SetComputeRoot32BitConstants(1, sizeof(c) / 4, &c, 0);

    // Transition intermediates to UAV
    Transition(cl, m_motionDepthClip, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    Transition(cl, m_yCocgColor, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

    // Transition inputs to SRV
    if (p.color) Transition(cl, p.color, D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    if (p.depth) Transition(cl, p.depth, D3D12_RESOURCE_STATE_DEPTH_READ, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    if (p.motion) Transition(cl, p.motion, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

    uint32_t dx = (m_renderW + 7) / 8;
    uint32_t dy = (m_renderH + 7) / 8;
    cl->Dispatch(dx, dy, 1);

    // UAV barrier between passes
    D3D12_UNORDERED_ACCESS_VIEW_BARRIER uavBarriers[2] = {};
    uavBarriers[0] = { m_motionDepthClip };
    uavBarriers[1] = { m_yCocgColor };
    // Use resource barriers instead
    Transition(cl, m_motionDepthClip, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    Transition(cl, m_yCocgColor, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

    // ── Pass 2: Upscale ──
    int histIdx = m_historyIdx;
    int prevIdx = 1 - histIdx;

    cl->SetComputeRootSignature(m_upscaleSig);
    cl->SetPipelineState(m_upscalePSO);
    cl->SetComputeRootDescriptorTable(0, m_srvHeap->GetGPUDescriptorHandleForHeapStart());

    // Set root constants for upscale pass
    struct UpscaleConstants {
        uint32_t renderW, renderH, displayW, displayH;
        float renderRcpX, renderRcpY, displayRcpX, displayRcpY;
        float jitterX, jitterY;
        float clipToPrevClip[4][4];
        float preExposure, cameraFovAngleHor, cameraNear, minLerpContrib;
        uint32_t sameCameraFrmNum, reset;
    } u{};
    u.renderW = p.renderWidth; u.renderH = p.renderHeight;
    u.displayW = p.displayWidth; u.displayH = p.displayHeight;
    u.renderRcpX = 1.0f / p.renderWidth; u.renderRcpY = 1.0f / p.renderHeight;
    u.displayRcpX = 1.0f / p.displayWidth; u.displayRcpY = 1.0f / p.displayHeight;
    u.jitterX = p.jitterX; u.jitterY = p.jitterY;
    memcpy(u.clipToPrevClip, p.clipToPrevClip, sizeof(float) * 16);
    u.preExposure = p.preExposure;
    u.cameraFovAngleHor = p.cameraFovAngleHor;
    u.cameraNear = p.cameraNear;
    u.minLerpContrib = p.minLerpContribution;
    u.sameCameraFrmNum = p.bSameCamera ? 1 : 0;
    u.reset = p.resetHistory ? 1 : 0;

    cl->SetComputeRoot32BitConstants(1, sizeof(u) / 4, &u, 0);

    // Transition output and history
    if (p.output) Transition(cl, p.output, D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    Transition(cl, m_history[histIdx], D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

    dx = (m_displayW + 7) / 8;
    dy = (m_displayH + 7) / 8;
    cl->Dispatch(dx, dy, 1);

    // Transition back
    if (p.output) Transition(cl, p.output, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_PRESENT);
    Transition(cl, m_history[histIdx], D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    if (p.color) Transition(cl, p.color, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_PRESENT);
    if (p.depth) Transition(cl, p.depth, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_DEPTH_READ);

    // Swap history buffers for next frame
    m_historyIdx = prevIdx;
    m_historyValid = true;
}

void SGSR2Pass::Shutdown() {
    if (m_convertPSO) { m_convertPSO->Release(); m_convertPSO = nullptr; }
    if (m_convertSig) { m_convertSig->Release(); m_convertSig = nullptr; }
    if (m_upscalePSO) { m_upscalePSO->Release(); m_upscalePSO = nullptr; }
    if (m_upscaleSig) { m_upscaleSig->Release(); m_upscaleSig = nullptr; }
    if (m_srvHeap) { m_srvHeap->Release(); m_srvHeap = nullptr; }
    if (m_samplerHeap) { m_samplerHeap->Release(); m_samplerHeap = nullptr; }
    if (m_motionDepthClip) { m_motionDepthClip->Release(); m_motionDepthClip = nullptr; }
    if (m_yCocgColor) { m_yCocgColor->Release(); m_yCocgColor = nullptr; }
    for (int i = 0; i < 2; i++) { if (m_history[i]) { m_history[i]->Release(); m_history[i] = nullptr; } }
    m_device = nullptr; m_initialized = false;
}

bool SGSR2Pass::Resize(uint32_t rW, uint32_t rH, uint32_t dW, uint32_t dH) {
    m_renderW = rW; m_renderH = rH; m_displayW = dW; m_displayH = dH;
    if (m_motionDepthClip) { m_motionDepthClip->Release(); m_motionDepthClip = nullptr; }
    if (m_yCocgColor) { m_yCocgColor->Release(); m_yCocgColor = nullptr; }
    for (int i = 0; i < 2; i++) { if (m_history[i]) { m_history[i]->Release(); m_history[i] = nullptr; } }
    if (m_srvHeap) { m_srvHeap->Release(); m_srvHeap = nullptr; }
    m_historyValid = false;
    return CreateIntermediateResources();
}

void SGSR2Pass::Transition(ID3D12GraphicsCommandList* cl, ID3D12Resource* res,
                           D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after) {
    if (before == after) return;
    auto b = SGSR2Barrier::Transition(res, before, after);
    cl->ResourceBarrier(1, &b);
}

bool SGSR2Pass::CreateConvertPipeline() {
    // Root sig: Table(SRV: color, depth, motion) + Constants + Table(Sampler)
    D3D12_ROOT_PARAMETER1 params[3]{};
    D3D12_DESCRIPTOR_RANGE1 srvRange{};
    srvRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    srvRange.NumDescriptors = 3; srvRange.BaseShaderRegister = 0;
    srvRange.RegisterSpace = 0; srvRange.Flags = D3D12_DESCRIPTOR_RANGE_FLAG_NONE;
    srvRange.OffsetInDescriptorsFromTableStart = 0;
    params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    params[0].DescriptorTable.NumDescriptorRanges = 1;
    params[0].DescriptorTable.pDescriptorRanges = &srvRange;
    params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    // Large constant buffer for SGSR2 params
    params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    params[1].Constants.ShaderRegister = 0; params[1].Constants.RegisterSpace = 0;
    params[1].Constants.Num32BitValues = 64; // Enough for all convert params
    params[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    D3D12_STATIC_SAMPLER_DESC sampler{};
    sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
    sampler.AddressU = sampler.AddressV = sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.ShaderRegister = 0; sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    D3D12_VERSIONED_ROOT_SIGNATURE_DESC rsDesc{};
    rsDesc.Version = D3D_ROOT_SIGNATURE_VERSION_1_1;
    rsDesc.Desc_1_1.NumParameters = 2; rsDesc.Desc_1_1.pParameters = params;
    rsDesc.Desc_1_1.NumStaticSamplers = 1; rsDesc.Desc_1_1.pStaticSamplers = &sampler;
    rsDesc.Desc_1_1.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;

    ID3DBlob* sigBlob = nullptr, *errBlob = nullptr;
    HRESULT hr = D3D12SerializeVersionedRootSignature(&rsDesc, &sigBlob, &errBlob);
    if (FAILED(hr)) { if (errBlob) AD_LOG_E("SGSR2 Convert RootSig: %s", (char*)errBlob->GetBufferPointer()); return false; }
    hr = m_device->CreateRootSignature(0, sigBlob->GetBufferPointer(), sigBlob->GetBufferSize(), IID_ID3D12RootSignature, (void**)&m_convertSig);
    sigBlob->Release();
    if (FAILED(hr)) return false;

    ID3DBlob* shaderBlob = nullptr;
    hr = D3DCompile(shaders::sgsr2_convert, shaders::sgsr2_convert_size, "sgsr2_convert", nullptr, nullptr, "CSMain", "cs_5_0", 0, 0, &shaderBlob, &errBlob);
    if (FAILED(hr)) { if (errBlob) AD_LOG_E("SGSR2 Convert compile: %s", (char*)errBlob->GetBufferPointer()); return false; }
    D3D12_COMPUTE_PIPELINE_STATE_DESC psoDesc{};
    psoDesc.pRootSignature = m_convertSig;
    psoDesc.CS.pShaderBytecode = shaderBlob->GetBufferPointer(); psoDesc.CS.BytecodeLength = shaderBlob->GetBufferSize();
    hr = m_device->CreateComputePipelineState(&psoDesc, IID_ID3D12PipelineState, (void**)&m_convertPSO);
    shaderBlob->Release();
    return SUCCEEDED(hr);
}

bool SGSR2Pass::CreateUpscalePipeline() {
    D3D12_ROOT_PARAMETER1 params[3]{};
    D3D12_DESCRIPTOR_RANGE1 srvRange{};
    srvRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    srvRange.NumDescriptors = 4; srvRange.BaseShaderRegister = 1;
    srvRange.RegisterSpace = 0; srvRange.Flags = D3D12_DESCRIPTOR_RANGE_FLAG_NONE;
    srvRange.OffsetInDescriptorsFromTableStart = 0;
    params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    params[0].DescriptorTable.NumDescriptorRanges = 1;
    params[0].DescriptorTable.pDescriptorRanges = &srvRange;
    params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    params[1].Constants.ShaderRegister = 0; params[1].Constants.RegisterSpace = 0;
    params[1].Constants.Num32BitValues = 64;
    params[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    D3D12_STATIC_SAMPLER_DESC sampler{};
    sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
    sampler.AddressU = sampler.AddressV = sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.ShaderRegister = 0; sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    D3D12_VERSIONED_ROOT_SIGNATURE_DESC rsDesc{};
    rsDesc.Version = D3D_ROOT_SIGNATURE_VERSION_1_1;
    rsDesc.Desc_1_1.NumParameters = 2; rsDesc.Desc_1_1.pParameters = params;
    rsDesc.Desc_1_1.NumStaticSamplers = 1; rsDesc.Desc_1_1.pStaticSamplers = &sampler;
    rsDesc.Desc_1_1.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;

    ID3DBlob* sigBlob = nullptr, *errBlob = nullptr;
    HRESULT hr = D3D12SerializeVersionedRootSignature(&rsDesc, &sigBlob, &errBlob);
    if (FAILED(hr)) return false;
    hr = m_device->CreateRootSignature(0, sigBlob->GetBufferPointer(), sigBlob->GetBufferSize(), IID_ID3D12RootSignature, (void**)&m_upscaleSig);
    sigBlob->Release();
    if (FAILED(hr)) return false;

    ID3DBlob* shaderBlob = nullptr;
    hr = D3DCompile(shaders::sgsr2_upscale, shaders::sgsr2_upscale_size, "sgsr2_upscale", nullptr, nullptr, "CSMain", "cs_5_0", 0, 0, &shaderBlob, &errBlob);
    if (FAILED(hr)) { if (errBlob) AD_LOG_E("SGSR2 Upscale compile: %s", (char*)errBlob->GetBufferPointer()); return false; }
    D3D12_COMPUTE_PIPELINE_STATE_DESC psoDesc{};
    psoDesc.pRootSignature = m_upscaleSig;
    psoDesc.CS.pShaderBytecode = shaderBlob->GetBufferPointer(); psoDesc.CS.BytecodeLength = shaderBlob->GetBufferSize();
    hr = m_device->CreateComputePipelineState(&psoDesc, IID_ID3D12PipelineState, (void**)&m_upscalePSO);
    shaderBlob->Release();
    return SUCCEEDED(hr);
}

bool SGSR2Pass::CreateIntermediateResources() {
    if (!m_device) return false;
    D3D12_HEAP_PROPERTIES defaultHeap{}; defaultHeap.Type = D3D12_HEAP_TYPE_DEFAULT;

    // MotionDepthClip buffer at render resolution (RGBA16F)
    D3D12_RESOURCE_DESC mdcDesc{};
    mdcDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    mdcDesc.Width = m_renderW; mdcDesc.Height = m_renderH;
    mdcDesc.DepthOrArraySize = 1; mdcDesc.MipLevels = 1;
    mdcDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
    mdcDesc.SampleDesc.Count = 1; mdcDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

    HRESULT hr = m_device->CreateCommittedResource(&defaultHeap, D3D12_HEAP_FLAG_NONE, &mdcDesc,
        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, nullptr, IID_ID3D12Resource, (void**)&m_motionDepthClip);
    if (FAILED(hr)) { AD_LOG_E("SGSR2: Failed to create MotionDepthClip"); return false; }

    // YCoCg color at render resolution (R32UI)
    D3D12_RESOURCE_DESC yccDesc = mdcDesc;
    yccDesc.Format = DXGI_FORMAT_R32_UINT;
    hr = m_device->CreateCommittedResource(&defaultHeap, D3D12_HEAP_FLAG_NONE, &yccDesc,
        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, nullptr, IID_ID3D12Resource, (void**)&m_yCocgColor);
    if (FAILED(hr)) { AD_LOG_E("SGSR2: Failed to create YCoCgColor"); return false; }

    // History textures at display resolution
    D3D12_RESOURCE_DESC histDesc{};
    histDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    histDesc.Width = m_displayW; histDesc.Height = m_displayH;
    histDesc.DepthOrArraySize = 1; histDesc.MipLevels = 1;
    histDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
    histDesc.SampleDesc.Count = 1; histDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

    for (int i = 0; i < 2; i++) {
        hr = m_device->CreateCommittedResource(&defaultHeap, D3D12_HEAP_FLAG_NONE, &histDesc,
            D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, nullptr, IID_ID3D12Resource, (void**)&m_history[i]);
        if (FAILED(hr)) { AD_LOG_E("SGSR2: Failed to create history[%d]", i); return false; }
    }

    AD_LOG_I("SGSR2: Intermediate resources created (render=%ux%u display=%ux%u)", m_renderW, m_renderH, m_displayW, m_displayH);
    return true;
}

} // namespace adrena
