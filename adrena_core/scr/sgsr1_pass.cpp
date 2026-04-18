#include "adrena_core/sgsr1_pass.h"
#include "adrena_core/logger.h"
#include "embedded_shaders.h"
#include <d3dcompiler.h>

namespace adrena {

static D3D12_RESOURCE_BARRIER MakeTransitionBarrier(ID3D12Resource* res,
    D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after,
    UINT sub = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES) {
    D3D12_RESOURCE_BARRIER b = {};
    b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    b.Transition.pResource = res;
    b.Transition.StateBefore = before;
    b.Transition.StateAfter = after;
    b.Transition.Subresource = sub;
    return b;
}

SGSR1Pass::~SGSR1Pass() { Shutdown(); }

bool SGSR1Pass::Init(ID3D12Device* device, DXGI_FORMAT fmt,
                     uint32_t rW, uint32_t rH, uint32_t dW, uint32_t dH) {
    if (m_initialized) return true;
    if (!device) return false;
    m_device = device;
    m_format = fmt;
    m_renderW = rW; m_renderH = rH;
    m_displayW = dW; m_displayH = dH;
    if (!CreatePipeline()) return false;
    if (!CreateIntermediate()) return false;
    m_initialized = true;
    AD_LOG_I("SGSR1Pass initialized: %ux%u -> %ux%u fmt=%u", rW, rH, dW, dH, (uint32_t)fmt);
    return true;
}

void SGSR1Pass::Execute(ID3D12GraphicsCommandList* cl, const SGSRParams& p) {
    if (!m_initialized || !cl || !p.color || !p.output || !m_intermediate) return;

    UINT descSize = m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle = m_srvHeap->GetCPUDescriptorHandleForHeapStart();
    D3D12_GPU_DESCRIPTOR_HANDLE gpuHandle = m_srvHeap->GetGPUDescriptorHandleForHeapStart();

    // Step 1: Copy backbuffer → intermediate (so we can read from intermediate, write to backbuffer)
    Transition(cl, p.color, D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_COPY_SOURCE);
    Transition(cl, m_intermediate, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COPY_DEST);
    cl->CopyResource(m_intermediate, p.color);

    // Step 2: Transition for compute — intermediate as SRV, backbuffer as UAV
    Transition(cl, m_intermediate, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    Transition(cl, p.color, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

    // Step 3: Create SRV for intermediate (slot 0)
    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Format = m_format;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MipLevels = 1;
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    m_device->CreateShaderResourceView(m_intermediate, &srvDesc, cpuHandle);

    // Create UAV for backbuffer (slot 2)
    D3D12_CPU_DESCRIPTOR_HANDLE uavCpu = cpuHandle;
    uavCpu.ptr += descSize * 2;
    D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
    uavDesc.Format = m_format;
    uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
    m_device->CreateUnorderedAccessView(p.color, nullptr, &uavDesc, uavCpu);

    // Step 4: Dispatch compute shader
    cl->SetComputeRootSignature(m_rootSig);
    cl->SetPipelineState(m_pso);
    ID3D12DescriptorHeap* heaps[] = { m_srvHeap };
    cl->SetDescriptorHeaps(1, heaps);
    cl->SetComputeRootDescriptorTable(0, gpuHandle);

    struct { uint32_t rW, rH, dW, dH; float sharp; uint32_t frame; } consts;
    consts.rW = p.renderWidth; consts.rH = p.renderHeight;
    consts.dW = p.displayWidth; consts.dH = p.displayHeight;
    consts.sharp = p.sharpness; consts.frame = 0;
    cl->SetComputeRoot32BitConstants(2, 6, &consts, 0);

    uint32_t dx = (m_displayW + 7) / 8;
    uint32_t dy = (m_displayH + 7) / 8;
    cl->Dispatch(dx, dy, 1);

    // Step 5: Transition backbuffer back to PRESENT
    Transition(cl, p.color, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_PRESENT);
}

void SGSR1Pass::Shutdown() {
    if (m_pso) { m_pso->Release(); m_pso = nullptr; }
    if (m_rootSig) { m_rootSig->Release(); m_rootSig = nullptr; }
    if (m_srvHeap) { m_srvHeap->Release(); m_srvHeap = nullptr; }
    if (m_samplerHeap) { m_samplerHeap->Release(); m_samplerHeap = nullptr; }
    if (m_intermediate) { m_intermediate->Release(); m_intermediate = nullptr; }
    m_device = nullptr;
    m_initialized = false;
}

bool SGSR1Pass::Resize(uint32_t rW, uint32_t rH, uint32_t dW, uint32_t dH) {
    m_renderW = rW; m_renderH = rH;
    m_displayW = dW; m_displayH = dH;
    if (m_intermediate) { m_intermediate->Release(); m_intermediate = nullptr; }
    if (m_srvHeap) { m_srvHeap->Release(); m_srvHeap = nullptr; }
    return CreateIntermediate();
}

void SGSR1Pass::Transition(ID3D12GraphicsCommandList* cl, ID3D12Resource* res,
                           D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after) {
    if (before == after) return;
    auto b = MakeTransitionBarrier(res, before, after);
    cl->ResourceBarrier(1, &b);
}

bool SGSR1Pass::CreatePipeline() {
    // Root signature: Table(SRV+UAV), Table(Sampler), Constants(6)
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
        if (errBlob) AD_LOG_E("RootSig serialize: %s", (char*)errBlob->GetBufferPointer());
        return false;
    }
    hr = m_device->CreateRootSignature(0, sigBlob->GetBufferPointer(),
        sigBlob->GetBufferSize(), IID_ID3D12RootSignature, (void**)&m_rootSig);
    sigBlob->Release();
    if (FAILED(hr)) { AD_LOG_E("CreateRootSignature failed: 0x%08X", hr); return false; }

    // Compile shader
    ID3DBlob* shaderBlob = nullptr;
    hr = D3DCompile(shaders::sgsr1_easu, shaders::sgsr1_easu_size,
        "sgsr1_easu", nullptr, nullptr, "CSMain", "cs_5_0", 0, 0, &shaderBlob, &errBlob);
    if (FAILED(hr)) {
        if (errBlob) AD_LOG_E("SGSR1 compile: %s", (char*)errBlob->GetBufferPointer());
        return false;
    }

    D3D12_COMPUTE_PIPELINE_STATE_DESC psoDesc{};
    psoDesc.pRootSignature = m_rootSig;
    psoDesc.CS.pShaderBytecode = shaderBlob->GetBufferPointer();
    psoDesc.CS.BytecodeLength = shaderBlob->GetBufferSize();
    hr = m_device->CreateComputePipelineState(&psoDesc, IID_ID3D12PipelineState, (void**)&m_pso);
    shaderBlob->Release();
    if (FAILED(hr)) { AD_LOG_E("CreateComputePipelineState failed: 0x%08X", hr); return false; }

    AD_LOG_I("SGSR1 compute pipeline created");
    return true;
}

bool SGSR1Pass::CreateIntermediate() {
    D3D12_DESCRIPTOR_HEAP_DESC srvDesc{};
    srvDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    srvDesc.NumDescriptors = 3; // 2 SRV + 1 UAV
    srvDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    HRESULT hr = m_device->CreateDescriptorHeap(&srvDesc, IID_ID3D12DescriptorHeap, (void**)&m_srvHeap);
    if (FAILED(hr)) return false;

    D3D12_DESCRIPTOR_HEAP_DESC sampDesc{};
    sampDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER;
    sampDesc.NumDescriptors = 1;
    sampDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    hr = m_device->CreateDescriptorHeap(&sampDesc, IID_ID3D12DescriptorHeap, (void**)&m_samplerHeap);
    if (FAILED(hr)) return false;

    // Intermediate texture — same size as display (backbuffer copy target)
    D3D12_RESOURCE_DESC texDesc{};
    texDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    texDesc.Width = m_displayW;
    texDesc.Height = m_displayH;
    texDesc.DepthOrArraySize = 1;
    texDesc.MipLevels = 1;
    texDesc.Format = m_format;
    texDesc.SampleDesc.Count = 1;
    texDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

    D3D12_HEAP_PROPERTIES heapProps{};
    heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;
    hr = m_device->CreateCommittedResource(&heapProps, D3D12_HEAP_FLAG_NONE, &texDesc,
        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, nullptr, IID_ID3D12Resource, (void**)&m_intermediate);
    if (FAILED(hr)) { AD_LOG_E("CreateIntermediate resource failed"); return false; }

    return true;
}

} // namespace adrena