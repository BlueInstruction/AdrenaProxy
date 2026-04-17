#include "proxy_swapchain.h"
#include "config.h"
#include "logger.h"
#include "overlay_menu.h"
#include "embedded_shaders.h"
#include <d3dcompiler.h>

#ifdef ADRENA_OVERLAY_ENABLED
#include "imgui.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_dx11.h"
#ifdef ADRENA_DX12_OVERLAY
#include "imgui_impl_dx12.h"
#endif
// Forward declaration for WndProc handler
LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
#endif

namespace adrena {

// Static member initialization
WNDPROC ProxySwapChain::s_origWndProc = nullptr;

// ─── Window Input Hook ─────────────────────────────────
LRESULT CALLBACK ProxySwapChain::WndProcHook(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    auto& overlay = GetOverlayMenu();
    auto& cfg = GetConfig();

    // Toggle Menu
    if (uMsg == WM_KEYDOWN && wParam == (WPARAM)cfg.toggle_key) {
        overlay.ToggleVisibility();
    }

#ifdef ADRENA_OVERLAY_ENABLED
    // Feed input to ImGui
    ImGui_ImplWin32_WndProcHandler(hWnd, uMsg, wParam, lParam);
#endif

    return CallWindowProcW(s_origWndProc, hWnd, uMsg, wParam, lParam);
}

void ProxySwapChain::HookWindow(HWND hwnd) {
    if (m_hwnd || !hwnd) return;
    m_hwnd = hwnd;
    GetOverlayMenu().Init(hwnd);
    s_origWndProc = (WNDPROC)SetWindowLongPtrW(hwnd, GWLP_WNDPROC, (LONG_PTR)WndProcHook);
    AD_LOG_I("Hooked Window WndProc (HWND=%p)", hwnd);
}

// ─── Constructor / Destructor ──────────────────────────
ProxySwapChain::ProxySwapChain(IDXGISwapChain1* real, IUnknown* device)
{
    // Query for IDXGISwapChain4 so all methods work
    if (FAILED(real->QueryInterface(__uuidof(IDXGISwapChain4), (void**)&m_real))) {
        m_real = nullptr;
        AD_LOG_E("Failed to get IDXGISwapChain4 from real swapchain");
        return;
    }
    real->Release(); // We transferred ref to m_real via QI

    if (SUCCEEDED(device->QueryInterface(__uuidof(ID3D11Device), (void**)&m_d3d11Device))) {
        m_isD3D12 = false;
        m_d3d11Device->GetImmediateContext(&m_d3d11Ctx);
        AD_LOG_I("SwapChain: D3D11 device detected");
    } else if (SUCCEEDED(device->QueryInterface(__uuidof(ID3D12CommandQueue), (void**)&m_commandQueue))) {
        m_isD3D12 = true;
        m_commandQueue->GetDevice(__uuidof(ID3D12Device), (void**)&m_d3d12Device);
        AD_LOG_I("SwapChain: D3D12 device detected");
    } else {
        AD_LOG_W("SwapChain: Unknown device type");
    }

    SetupResources();
}

ProxySwapChain::~ProxySwapChain() {
    TeardownResources();
    // Unhook WndProc
    if (m_hwnd && s_origWndProc) {
        SetWindowLongPtrW(m_hwnd, GWLP_WNDPROC, (LONG_PTR)s_origWndProc);
        s_origWndProc = nullptr;
    }
    if (m_real) m_real->Release();
    if (m_d3d11Device) m_d3d11Device->Release();
    if (m_d3d11Ctx) m_d3d11Ctx->Release();
    if (m_d3d12Device) m_d3d12Device->Release();
    if (m_commandQueue) m_commandQueue->Release();
    if (m_cmdList) m_cmdList->Release();
    if (m_cmdAlloc) m_cmdAlloc->Release();
    if (m_rootSig) m_rootSig->Release();
    if (m_computePSO) m_computePSO->Release();
    if (m_intermediateTex) m_intermediateTex->Release();
    if (m_computeHeap) m_computeHeap->Release();
    if (m_fence) m_fence->Release();
    if (m_fenceEvent) CloseHandle(m_fenceEvent);
    if (m_overlayCmdAlloc) m_overlayCmdAlloc->Release();
    if (m_overlayCmdList) m_overlayCmdList->Release();
    if (m_rtvHeap) m_rtvHeap->Release();
}

HRESULT ProxySwapChain::QueryInterface(REFIID riid, void** ppv) {
    if (!ppv) return E_POINTER;
    if (riid == __uuidof(IDXGISwapChain) || riid == __uuidof(IDXGISwapChain1) ||
        riid == __uuidof(IDXGISwapChain2) || riid == __uuidof(IDXGISwapChain3) ||
        riid == __uuidof(IDXGISwapChain4) || riid == __uuidof(IDXGIDeviceSubObject) ||
        riid == __uuidof(IDXGIObject) || riid == __uuidof(IUnknown)) {
        *ppv = this; AddRef(); return S_OK;
    }
    return m_real->QueryInterface(riid, ppv);
}

ULONG ProxySwapChain::AddRef() { return InterlockedIncrement(&m_refCount); }
ULONG ProxySwapChain::Release() { ULONG c = InterlockedDecrement(&m_refCount); if (c == 0) delete this; return c; }

HRESULT ProxySwapChain::SetPrivateData(REFGUID n, UINT d, const void* p) { return m_real->SetPrivateData(n,d,p); }
HRESULT ProxySwapChain::SetPrivateDataInterface(REFGUID n, const IUnknown* p) { return m_real->SetPrivateDataInterface(n,p); }
HRESULT ProxySwapChain::GetPrivateData(REFGUID n, UINT* s, void* p) { return m_real->GetPrivateData(n,s,p); }
HRESULT ProxySwapChain::GetParent(REFIID r, void** p) { return m_real->GetParent(r,p); }
HRESULT ProxySwapChain::GetDevice(REFIID r, void** p) { return m_real->GetDevice(r,p); }

HRESULT ProxySwapChain::GetBuffer(UINT idx, REFIID riid, void** ppBuf) {
    auto& cfg = GetConfig();
    // D3D11: redirect game to low-res render target
    if (cfg.enabled && cfg.sgsr_mode != SGSRMode::Off && m_lowresRT11 && riid == __uuidof(ID3D11Texture2D)) {
        return m_lowresRT11->QueryInterface(riid, ppBuf);
    }
    // D3D12: redirect game to low-res render target
    if (cfg.enabled && cfg.sgsr_mode != SGSRMode::Off && m_lowresRT12 && riid == __uuidof(ID3D12Resource)) {
        return m_lowresRT12->QueryInterface(riid, ppBuf);
    }
    return m_real->GetBuffer(idx, riid, ppBuf);
}

HRESULT ProxySwapChain::Present(UINT SyncInterval, UINT Flags) { return HookPresent(SyncInterval, Flags); }
HRESULT ProxySwapChain::Present1(UINT SyncInterval, UINT Flags, const DXGI_PRESENT_PARAMETERS*) { return HookPresent(SyncInterval, Flags); }

HRESULT ProxySwapChain::HookPresent(UINT SyncInterval, UINT Flags) {
    auto& cfg = GetConfig();

    // Hook window on first present
    if (!m_hwnd) {
        DXGI_SWAP_CHAIN_DESC desc = {};
        if (SUCCEEDED(m_real->GetDesc(&desc)) && desc.OutputWindow) {
            HookWindow(desc.OutputWindow);
        }
    }

    if (cfg.enabled && cfg.sgsr_mode != SGSRMode::Off && m_initialized) {
        if (m_isD3D12) ProcessSGSR12(); else ProcessSGSR11();
    }
#ifdef ADRENA_OVERLAY_ENABLED
    if (cfg.overlay_enabled && m_hwnd) RenderOverlay();
#endif
    return m_real->Present(SyncInterval, Flags);
}

void ProxySwapChain::ProcessSGSR11() {
    if (!m_d3d11Ctx || !m_lowresRT11) return;
    ID3D11Texture2D* backBuffer = nullptr;
    if (SUCCEEDED(m_real->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&backBuffer))) {
        D3D11_BOX srcBox = { 0, 0, 0, m_renderWidth, m_renderHeight, 1 };
        m_d3d11Ctx->CopySubresourceRegion(backBuffer, 0, 0, 0, 0, m_lowresRT11, 0, &srcBox);
        backBuffer->Release();
    }
}

// ─── D3D12 SGSR Compute Pipeline ────────────────────────

void ProxySwapChain::InitD3D12Compute() {
    if (!m_d3d12Device || m_computePSO) return;
    AD_LOG_I("Initializing D3D12 Compute Pipeline for SGSR1...");

    D3D12_DESCRIPTOR_RANGE ranges[2] = {};
    ranges[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV; ranges[0].NumDescriptors = 1; ranges[0].BaseShaderRegister = 0;
    ranges[1].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV; ranges[1].NumDescriptors = 1; ranges[1].BaseShaderRegister = 0;

    D3D12_ROOT_PARAMETER rootParams[3] = {};
    rootParams[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS; rootParams[0].Constants.ShaderRegister = 0; rootParams[0].Constants.Num32BitValues = 4; rootParams[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    rootParams[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE; rootParams[1].DescriptorTable.NumDescriptorRanges = 1; rootParams[1].DescriptorTable.pDescriptorRanges = &ranges[0]; rootParams[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    rootParams[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE; rootParams[2].DescriptorTable.NumDescriptorRanges = 1; rootParams[2].DescriptorTable.pDescriptorRanges = &ranges[1]; rootParams[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    D3D12_STATIC_SAMPLER_DESC sampler = {}; sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR; sampler.AddressU = sampler.AddressV = sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP; sampler.ShaderRegister = 0; sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    D3D12_ROOT_SIGNATURE_DESC rsDesc = {}; rsDesc.NumParameters = 3; rsDesc.pParameters = rootParams; rsDesc.NumStaticSamplers = 1; rsDesc.pStaticSamplers = &sampler; rsDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;
    ID3DBlob* sigBlob = nullptr; ID3DBlob* errBlob = nullptr;
    HRESULT hr = D3D12SerializeRootSignature(&rsDesc, D3D_ROOT_SIGNATURE_VERSION_1, &sigBlob, &errBlob);
    if (FAILED(hr)) { AD_LOG_E("Root Sig Error: %s", errBlob ? (char*)errBlob->GetBufferPointer() : "Unknown"); if (errBlob) errBlob->Release(); return; }
    m_d3d12Device->CreateRootSignature(0, sigBlob->GetBufferPointer(), sigBlob->GetBufferSize(), IID_PPV_ARGS(&m_rootSig));
    sigBlob->Release();

    ID3DBlob* shaderBlob = nullptr;
    hr = D3DCompile(adrena::shaders::sgsr1_easu, adrena::shaders::sgsr1_easu_size, "sgsr1_easu.hlsl", nullptr, nullptr, "CSMain", "cs_5_0", 0, 0, &shaderBlob, &errBlob);
    if (FAILED(hr)) { AD_LOG_E("SGSR1 Shader Compile Failed: %s", errBlob ? (char*)errBlob->GetBufferPointer() : "Unknown"); if (errBlob) errBlob->Release(); return; }

    D3D12_COMPUTE_PIPELINE_STATE_DESC psoDesc = {}; psoDesc.pRootSignature = m_rootSig; psoDesc.CS = { shaderBlob->GetBufferPointer(), shaderBlob->GetBufferSize() };
    m_d3d12Device->CreateComputePipelineState(&psoDesc, IID_PPV_ARGS(&m_computePSO));
    shaderBlob->Release();

    D3D12_DESCRIPTOR_HEAP_DESC heapDesc = {}; heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV; heapDesc.NumDescriptors = 2; heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    m_d3d12Device->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&m_computeHeap));

    // Intermediate texture at render (low-res) dimensions for SGSR input
    D3D12_RESOURCE_DESC texDesc = {}; texDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D; texDesc.Width = m_renderWidth; texDesc.Height = m_renderHeight; texDesc.DepthOrArraySize = 1; texDesc.MipLevels = 1; texDesc.Format = m_format; texDesc.SampleDesc = { 1, 0 }; texDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    D3D12_HEAP_PROPERTIES heapProps = {}; heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;
    m_d3d12Device->CreateCommittedResource(&heapProps, D3D12_HEAP_FLAG_NONE, &texDesc, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, nullptr, IID_PPV_ARGS(&m_intermediateTex));

    m_d3d12Device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_fence)); m_fenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
    AD_LOG_I("D3D12 Compute Pipeline initialized! (%ux%u -> %ux%u fmt=%u)", m_renderWidth, m_renderHeight, m_displayWidth, m_displayHeight, m_format);
}

void ProxySwapChain::ExecuteD3D12Compute() {
    // Sync: wait for GPU to finish previous frame
    m_fenceValue++; m_commandQueue->Signal(m_fence, m_fenceValue);
    if (m_fence->GetCompletedValue() < m_fenceValue) { m_fence->SetEventOnCompletion(m_fenceValue, m_fenceEvent); WaitForSingleObject(m_fenceEvent, INFINITE); }

    m_cmdAlloc->Reset(); m_cmdList->Reset(m_cmdAlloc, m_computePSO);

    // Source: m_lowresRT12 (game rendered here via GetBuffer redirect)
    // Destination: real backbuffer at display resolution
    ID3D12Resource* backBuffer = nullptr; UINT index = m_real->GetCurrentBackBufferIndex(); m_real->GetBuffer(index, IID_PPV_ARGS(&backBuffer));

    // Transition lowresRT12: RENDER_TARGET -> COPY_SOURCE
    // Transition intermediateTex: NON_PIXEL_SHADER_RESOURCE -> COPY_DEST
    D3D12_RESOURCE_BARRIER barriers[2] = {};
    barriers[0].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION; barriers[0].Transition.pResource = m_lowresRT12; barriers[0].Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET; barriers[0].Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE; barriers[0].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barriers[1].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION; barriers[1].Transition.pResource = m_intermediateTex; barriers[1].Transition.StateBefore = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE; barriers[1].Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST; barriers[1].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    m_cmdList->ResourceBarrier(2, barriers);

    // Copy low-res game output into intermediate texture (SGSR SRV input)
    m_cmdList->CopyResource(m_intermediateTex, m_lowresRT12);

    // Transition intermediateTex: COPY_DEST -> SRV, backBuffer: PRESENT -> UAV
    barriers[0].Transition.pResource = m_intermediateTex; barriers[0].Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST; barriers[0].Transition.StateAfter = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    barriers[1].Transition.pResource = backBuffer; barriers[1].Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT; barriers[1].Transition.StateAfter = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    m_cmdList->ResourceBarrier(2, barriers);

    // Create descriptors
    UINT descSize = m_d3d12Device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    D3D12_CPU_DESCRIPTOR_HANDLE hCPU = m_computeHeap->GetCPUDescriptorHandleForHeapStart();
    D3D12_GPU_DESCRIPTOR_HANDLE hGPU = m_computeHeap->GetGPUDescriptorHandleForHeapStart();

    // SRV: intermediate (low-res input)
    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {}; srvDesc.Format = m_format; srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D; srvDesc.Texture2D.MipLevels = 1; srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    m_d3d12Device->CreateShaderResourceView(m_intermediateTex, &srvDesc, hCPU); hCPU.ptr += descSize;

    // UAV: backbuffer (full-res output)
    D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {}; uavDesc.Format = m_format; uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
    m_d3d12Device->CreateUnorderedAccessView(backBuffer, nullptr, &uavDesc, hCPU);

    // Dispatch compute
    ID3D12DescriptorHeap* heaps[] = { m_computeHeap }; m_cmdList->SetDescriptorHeaps(1, heaps);
    m_cmdList->SetComputeRootSignature(m_rootSig);
    // constants: [renderW, renderH, displayW, displayH] — actual upscaling dimensions
    uint32_t constants[4] = { m_renderWidth, m_renderHeight, m_displayWidth, m_displayHeight };
    m_cmdList->SetComputeRoot32BitConstants(0, 4, constants, 0);
    m_cmdList->SetComputeRootDescriptorTable(1, hGPU);
    m_cmdList->SetComputeRootDescriptorTable(2, D3D12_GPU_DESCRIPTOR_HANDLE{ hGPU.ptr + descSize });
    m_cmdList->Dispatch((m_displayWidth + 7) / 8, (m_displayHeight + 7) / 8, 1);

    // Transition backBuffer: UAV -> PRESENT, lowresRT12: COPY_SOURCE -> RENDER_TARGET
    barriers[0].Transition.pResource = backBuffer; barriers[0].Transition.StateBefore = D3D12_RESOURCE_STATE_UNORDERED_ACCESS; barriers[0].Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
    barriers[1].Transition.pResource = m_lowresRT12; barriers[1].Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_SOURCE; barriers[1].Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
    m_cmdList->ResourceBarrier(2, barriers);

    m_cmdList->Close();
    ID3D12CommandList* lists[] = { m_cmdList }; m_commandQueue->ExecuteCommandLists(1, lists);
    m_fenceValue++; m_commandQueue->Signal(m_fence, m_fenceValue);
    backBuffer->Release();
}

void ProxySwapChain::ProcessSGSR12() {
    if (!m_d3d12Device || !m_commandQueue || !m_lowresRT12) return;
    if (!m_computePSO) InitD3D12Compute();
    if (!m_computePSO) return;
    if (!m_cmdAlloc) m_d3d12Device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&m_cmdAlloc));
    if (!m_cmdList) { m_d3d12Device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, m_cmdAlloc, nullptr, IID_PPV_ARGS(&m_cmdList)); m_cmdList->Close(); }
    ExecuteD3D12Compute();
}

// ─── Overlay Rendering ──────────────────────────────────

void ProxySwapChain::RenderOverlay() {
#ifdef ADRENA_OVERLAY_ENABLED
    auto& overlay = GetOverlayMenu();
    overlay.UpdateFPS();

    if (!m_isD3D12) {
        overlay.RenderFrame11(m_d3d11Device, m_d3d11Ctx, m_real);
        return;
    }
    // D3D11 fallback only — D3D12 overlay rendering is deferred to future work
    // For now, FPS counter and menu are only visible in D3D11 mode
#endif
}

void ProxySwapChain::SetupResources() {
    DXGI_SWAP_CHAIN_DESC1 desc = {};
    if (FAILED(m_real->GetDesc1(&desc))) return;
    m_displayWidth = desc.Width; m_displayHeight = desc.Height; m_format = desc.Format;
    auto& cfg = GetConfig(); cfg.ApplyRenderScale();
    m_renderWidth = (UINT)(m_displayWidth * cfg.render_scale);
    m_renderHeight = (UINT)(m_displayHeight * cfg.render_scale);

    if (!m_isD3D12 && m_d3d11Device) {
        D3D11_TEXTURE2D_DESC rtDesc = {};
        rtDesc.Width = m_renderWidth; rtDesc.Height = m_renderHeight; rtDesc.MipLevels = 1; rtDesc.ArraySize = 1;
        rtDesc.Format = m_format; rtDesc.SampleDesc = { 1, 0 }; rtDesc.Usage = D3D11_USAGE_DEFAULT;
        rtDesc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
        m_d3d11Device->CreateTexture2D(&rtDesc, nullptr, &m_lowresRT11);
    }

    if (m_isD3D12 && m_d3d12Device) {
        D3D12_RESOURCE_DESC rtDesc = {};
        rtDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        rtDesc.Width = m_renderWidth; rtDesc.Height = m_renderHeight;
        rtDesc.DepthOrArraySize = 1; rtDesc.MipLevels = 1;
        rtDesc.Format = m_format; rtDesc.SampleDesc = { 1, 0 };
        rtDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET | D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
        D3D12_HEAP_PROPERTIES heapProps = {}; heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;
        D3D12_CLEAR_VALUE clearVal = {}; clearVal.Format = m_format;
        HRESULT hr = m_d3d12Device->CreateCommittedResource(
            &heapProps, D3D12_HEAP_FLAG_NONE, &rtDesc,
            D3D12_RESOURCE_STATE_RENDER_TARGET, &clearVal,
            IID_PPV_ARGS(&m_lowresRT12));
        if (FAILED(hr)) {
            AD_LOG_E("Failed to create D3D12 low-res render target (hr=0x%X)", hr);
        } else {
            AD_LOG_I("D3D12 low-res RT created (%ux%u)", m_renderWidth, m_renderHeight);
        }
    }

    m_initialized = true;
}

void ProxySwapChain::TeardownResources() {
    if (m_lowresRT11) { m_lowresRT11->Release(); m_lowresRT11 = nullptr; }
    if (m_lowresRT12) { m_lowresRT12->Release(); m_lowresRT12 = nullptr; }
    m_initialized = false;
}

// Pass-through implementations (m_real is IDXGISwapChain4* now, so all methods exist)
HRESULT ProxySwapChain::SetFullscreenState(BOOL f, IDXGIOutput* t) { return m_real->SetFullscreenState(f,t); }
HRESULT ProxySwapChain::GetFullscreenState(BOOL* f, IDXGIOutput** t) { return m_real->GetFullscreenState(f,t); }
HRESULT ProxySwapChain::GetDesc(DXGI_SWAP_CHAIN_DESC* d) { return m_real->GetDesc(d); }
HRESULT ProxySwapChain::ResizeBuffers(UINT bc, UINT w, UINT h, DXGI_FORMAT f, UINT fl) { TeardownResources(); HRESULT hr = m_real->ResizeBuffers(bc,w,h,f,fl); if (SUCCEEDED(hr)) SetupResources(); return hr; }
HRESULT ProxySwapChain::ResizeTarget(const DXGI_MODE_DESC* d) { return m_real->ResizeTarget(d); }
HRESULT ProxySwapChain::GetContainingOutput(IDXGIOutput** o) { return m_real->GetContainingOutput(o); }
HRESULT ProxySwapChain::GetFrameStatistics(DXGI_FRAME_STATISTICS* s) { return m_real->GetFrameStatistics(s); }
HRESULT ProxySwapChain::GetLastPresentCount(UINT* c) { return m_real->GetLastPresentCount(c); }
HRESULT ProxySwapChain::GetDesc1(DXGI_SWAP_CHAIN_DESC1* d) { return m_real->GetDesc1(d); }
HRESULT ProxySwapChain::GetFullscreenDesc(DXGI_SWAP_CHAIN_FULLSCREEN_DESC* d) { return m_real->GetFullscreenDesc(d); }
HRESULT ProxySwapChain::GetHwnd(HWND* h) { return m_real->GetHwnd(h); }
HRESULT ProxySwapChain::GetCoreWindow(REFIID r, void** p) { return m_real->GetCoreWindow(r,p); }
BOOL ProxySwapChain::IsTemporaryMonoSupported() { return m_real->IsTemporaryMonoSupported(); }
HRESULT ProxySwapChain::GetRestrictToOutput(IDXGIOutput** o) { return m_real->GetRestrictToOutput(o); }
HRESULT ProxySwapChain::SetBackgroundColor(const DXGI_RGBA* c) { return m_real->SetBackgroundColor(c); }
HRESULT ProxySwapChain::GetBackgroundColor(DXGI_RGBA* c) { return m_real->GetBackgroundColor(c); }
HRESULT ProxySwapChain::SetRotation(DXGI_MODE_ROTATION r) { return m_real->SetRotation(r); }
HRESULT ProxySwapChain::GetRotation(DXGI_MODE_ROTATION* r) { return m_real->GetRotation(r); }
HRESULT ProxySwapChain::SetSourceSize(UINT w, UINT h) { return m_real->SetSourceSize(w,h); }
HRESULT ProxySwapChain::GetSourceSize(UINT* w, UINT* h) { return m_real->GetSourceSize(w,h); }
HRESULT ProxySwapChain::SetMaximumFrameLatency(UINT l) { return m_real->SetMaximumFrameLatency(l); }
HRESULT ProxySwapChain::GetMaximumFrameLatency(UINT* l) { return m_real->GetMaximumFrameLatency(l); }
HANDLE  ProxySwapChain::GetFrameLatencyWaitableObject() { return m_real->GetFrameLatencyWaitableObject(); }
HRESULT ProxySwapChain::SetMatrixTransform(const DXGI_MATRIX_3X2_F* m) { return m_real->SetMatrixTransform(m); }
HRESULT ProxySwapChain::GetMatrixTransform(DXGI_MATRIX_3X2_F* m) { return m_real->GetMatrixTransform(m); }
UINT    ProxySwapChain::GetCurrentBackBufferIndex() { return m_real->GetCurrentBackBufferIndex(); }
HRESULT ProxySwapChain::CheckColorSpaceSupport(DXGI_COLOR_SPACE_TYPE c, UINT* s) { return m_real->CheckColorSpaceSupport(c,s); }
HRESULT ProxySwapChain::SetColorSpace1(DXGI_COLOR_SPACE_TYPE c) { return m_real->SetColorSpace1(c); }
HRESULT ProxySwapChain::ResizeBuffers1(UINT bc, UINT w, UINT h, DXGI_FORMAT f, UINT fl, const UINT* n, IUnknown* const* q) { return m_real->ResizeBuffers1(bc,w,h,f,fl,n,q); }
HRESULT ProxySwapChain::SetHDRMetaData(DXGI_HDR_METADATA_TYPE t, UINT s, void* d) { return m_real->SetHDRMetaData(t,s,d); }

} // namespace adrena
