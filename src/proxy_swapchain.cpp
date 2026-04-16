#include "proxy_swapchain.h"
#include "config.h"
#include "logger.h"
#include "overlay_menu.h"

namespace adrena {

ProxySwapChain::ProxySwapChain(IDXGISwapChain1* real, IUnknown* device)
    : m_real(real)
{
    // Detect D3D11 vs D3D12
    if (SUCCEEDED(device->QueryInterface(__uuidof(ID3D11Device), (void**)&m_d3d11Device))) {
        m_isD3D12 = false;
        m_d3d11Device->GetImmediateContext(&m_d3d11Ctx);
        AD_LOG_I("SwapChain: D3D11 device detected");
    } else if (SUCCEEDED(device->QueryInterface(__uuidof(ID3D12CommandQueue), (void**)&m_commandQueue))) {
        m_isD3D12 = true;
        m_commandQueue->GetDevice(__uuidof(ID3D12Device), (void**)&m_d3d12Device);
        AD_LOG_I("SwapChain: D3D12 device detected (Queue=%p Device=%p)", m_commandQueue, m_d3d12Device);
    } else {
        AD_LOG_W("SwapChain: Unknown device type");
    }
}

ProxySwapChain::~ProxySwapChain()
{
    TeardownResources();
    if (m_real) m_real->Release();
    if (m_d3d11Device) m_d3d11Device->Release();
    if (m_d3d11Ctx) m_d3d11Ctx->Release();
    if (m_d3d12Device) m_d3d12Device->Release();
    if (m_commandQueue) m_commandQueue->Release();
    if (m_cmdList) m_cmdList->Release();
    if (m_cmdAlloc) m_cmdAlloc->Release();
}

// ─── IUnknown ────────────────────────────────────────
HRESULT ProxySwapChain::QueryInterface(REFIID riid, void** ppv)
{
    if (!ppv) return E_POINTER;
    if (riid == __uuidof(IDXGISwapChain) || riid == __uuidof(IDXGISwapChain1) ||
        riid == __uuidof(IDXGISwapChain2) || riid == __uuidof(IDXGISwapChain3) ||
        riid == __uuidof(IDXGISwapChain4) || riid == __uuidof(IDXGIDeviceSubObject) ||
        riid == __uuidof(IDXGIObject) || riid == __uuidof(IUnknown))
    {
        *ppv = this;
        AddRef();
        return S_OK;
    }
    return m_real->QueryInterface(riid, ppv);
}

ULONG ProxySwapChain::AddRef() { return InterlockedIncrement(&m_refCount); }
ULONG ProxySwapChain::Release()
{
    ULONG c = InterlockedDecrement(&m_refCount);
    if (c == 0) delete this;
    return c;
}

// ─── IDXGIObject pass-through ────────────────────────
HRESULT ProxySwapChain::SetPrivateData(REFGUID n, UINT d, const void* p) { return m_real->SetPrivateData(n,d,p); }
HRESULT ProxySwapChain::SetPrivateDataInterface(REFGUID n, const IUnknown* p) { return m_real->SetPrivateDataInterface(n,p); }
HRESULT ProxySwapChain::GetPrivateData(REFGUID n, UINT* s, void* p) { return m_real->GetPrivateData(n,s,p); }
HRESULT ProxySwapChain::GetParent(REFIID r, void** p) { return m_real->GetParent(r,p); }
HRESULT ProxySwapChain::GetDevice(REFIID r, void** p) { return m_real->GetDevice(r,p); }

// ─── IDXGISwapChain ─────────────────────────────────
HRESULT ProxySwapChain::GetBuffer(UINT idx, REFIID riid, void** ppBuf)
{
    auto& cfg = GetConfig();
    if (cfg.enabled && cfg.sgsr_mode != SGSRMode::Off) {
        // Return our low-res render target instead of the real backbuffer
        if (m_lowresRT11 && riid == __uuidof(ID3D11Texture2D)) {
            return m_lowresRT11->QueryInterface(riid, ppBuf);
        }
    }
    return m_real->GetBuffer(idx, riid, ppBuf);
}

HRESULT ProxySwapChain::Present(UINT SyncInterval, UINT Flags)
{
    return HookPresent(SyncInterval, Flags);
}

HRESULT ProxySwapChain::Present1(UINT SyncInterval, UINT Flags, const DXGI_PRESENT_PARAMETERS* params)
{
    return HookPresent(SyncInterval, Flags);
}

HRESULT ProxySwapChain::HookPresent(UINT SyncInterval, UINT Flags)
{
    auto& cfg = GetConfig();

    if (cfg.enabled && cfg.sgsr_mode != SGSRMode::Off && m_initialized) {
        if (m_isD3D12) {
            ProcessSGSR12();
        } else {
            ProcessSGSR11();
        }
    }

    // Render overlay on top
#ifdef ADRENA_OVERLAY_ENABLED
    if (cfg.overlay_enabled) {
        RenderOverlay();
    }
#endif

    return m_real->Present(SyncInterval, Flags);
}

// ─── SGSR11 Processing ──────────────────────────────
void ProxySwapChain::ProcessSGSR11()
{
    if (!m_d3d11Ctx || !m_lowresRT11 || !m_upscaledTex11) return;

    // Copy low-res RT to real backbuffer via SGSR
    // In a full implementation, this would dispatch SGSR compute shaders
    // For now, use a simple copy to the real backbuffer
    ID3D11Texture2D* backBuffer = nullptr;
    if (SUCCEEDED(m_real->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&backBuffer))) {
        D3D11_BOX srcBox = { 0, 0, 0, m_renderWidth, m_renderHeight, 1 };
        m_d3d11Ctx->CopySubresourceRegion(backBuffer, 0, 0, 0, 0, m_lowresRT11, 0, &srcBox);
        backBuffer->Release();
    }
}

// ─── SGSR12 Processing ──────────────────────────────
void ProxySwapChain::ProcessSGSR12()
{
    if (!m_d3d12Device || !m_commandQueue) return;

    // DX12 path: Use command list to copy/process
    if (!m_cmdAlloc) {
        m_d3d12Device->CreateCommandAllocator(
            D3D12_COMMAND_LIST_TYPE_DIRECT,
            __uuidof(ID3D12CommandAllocator), (void**)&m_cmdAlloc);
    }
    if (!m_cmdList) {
        m_d3d12Device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT,
            m_cmdAlloc, nullptr, __uuidof(ID3D12GraphicsCommandList), (void**)&m_cmdList);
    }

    // Reset and record commands
    m_cmdAlloc->Reset();
    m_cmdList->Reset(m_cmdAlloc, nullptr);

    // In full implementation: SGSR compute dispatch here
    // For now: resource barrier + copy as placeholder
    ID3D12Resource* backBuffer = nullptr;
    if (SUCCEEDED(m_real->GetBuffer(0, __uuidof(ID3D12Resource), (void**)&backBuffer))) {
        D3D12_RESOURCE_BARRIER barrier = {};
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition.pResource = backBuffer;
        barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
        barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;
        barrier.Transition.Subresource = 0;
        m_cmdList->ResourceBarrier(1, &barrier);

        // Placeholder: SGSR compute would run here
        // Then copy result to backbuffer

        barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
        barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
        m_cmdList->ResourceBarrier(1, &barrier);
        backBuffer->Release();
    }

    m_cmdList->Close();
    ID3D12CommandList* lists[] = { m_cmdList };
    m_commandQueue->ExecuteCommandLists(1, lists);
    m_commandQueue->Signal(nullptr, 0); // simplified
}

// ─── Overlay ─────────────────────────────────────────
void ProxySwapChain::RenderOverlay()
{
    auto& overlay = GetOverlayMenu();
    if (!overlay.IsVisible() && !GetConfig().fps_display) return;

    if (m_isD3D12) {
        overlay.RenderFrame12(m_d3d12Device, m_commandQueue, m_real);
    } else {
        overlay.RenderFrame11(m_d3d11Device, m_d3d11Ctx, m_real);
    }
}

// ─── Setup / Teardown ───────────────────────────────
void ProxySwapChain::SetupResources()
{
    DXGI_SWAP_CHAIN_DESC1 desc = {};
    if (FAILED(m_real->GetDesc1(&desc))) return;

    m_displayWidth = desc.Width;
    m_displayHeight = desc.Height;
    m_format = desc.Format;
    m_bufferCount = desc.BufferCount;

    auto& cfg = GetConfig();
    cfg.ApplyRenderScale();

    m_renderWidth = (UINT)(m_displayWidth * cfg.render_scale);
    m_renderHeight = (UINT)(m_displayHeight * cfg.render_scale);

    AD_LOG_I("Resources: Display=%ux%u Render=%ux%u (scale=%.2f)",
             m_displayWidth, m_displayHeight, m_renderWidth, m_renderHeight, cfg.render_scale);

    if (!m_isD3D12 && m_d3d11Device) {
        // Create low-res render target
        D3D11_TEXTURE2D_DESC rtDesc = {};
        rtDesc.Width = m_renderWidth;
        rtDesc.Height = m_renderHeight;
        rtDesc.MipLevels = 1;
        rtDesc.ArraySize = 1;
        rtDesc.Format = m_format;
        rtDesc.SampleDesc = { 1, 0 };
        rtDesc.Usage = D3D11_USAGE_DEFAULT;
        rtDesc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
        rtDesc.CPUAccessFlags = 0;
        rtDesc.MiscFlags = 0;

        m_d3d11Device->CreateTexture2D(&rtDesc, nullptr, &m_lowresRT11);
        AD_LOG_I("Created low-res RT: %ux%u", m_renderWidth, m_renderHeight);

        // Create upscaled texture for SGSR output
        rtDesc.Width = m_displayWidth;
        rtDesc.Height = m_displayHeight;
        rtDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
        m_d3d11Device->CreateTexture2D(&rtDesc, nullptr, &m_upscaledTex11);
    }

    m_initialized = true;
}

void ProxySwapChain::TeardownResources()
{
    if (m_lowresRT11) { m_lowresRT11->Release(); m_lowresRT11 = nullptr; }
    if (m_upscaledTex11) { m_upscaledTex11->Release(); m_upscaledTex11 = nullptr; }
    m_initialized = false;
}

// ─── Remaining IDXGISwapChain pass-through ──────────
HRESULT ProxySwapChain::SetFullscreenState(BOOL f, IDXGIOutput* t) { return m_real->SetFullscreenState(f,t); }
HRESULT ProxySwapChain::GetFullscreenState(BOOL* f, IDXGIOutput** t) { return m_real->GetFullscreenState(f,t); }
HRESULT ProxySwapChain::GetDesc(DXGI_SWAP_CHAIN_DESC* d) { return m_real->GetDesc(d); }

HRESULT ProxySwapChain::ResizeBuffers(UINT bc, UINT w, UINT h, DXGI_FORMAT f, UINT fl)
{
    TeardownResources();
    HRESULT hr = m_real->ResizeBuffers(bc, w, h, f, fl);
    if (SUCCEEDED(hr)) SetupResources();
    return hr;
}

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
HRESULT ProxySwapChain::ResizeBuffers1(UINT bc, UINT w, UINT h, DXGI_FORMAT f, UINT fl,
    const UINT* n, IUnknown* const* q) { return m_real->ResizeBuffers1(bc,w,h,f,fl,n,q); }
HRESULT ProxySwapChain::SetHDRMetaData(DXGI_HDR_METADATA_TYPE t, UINT s, void* d) { return m_real->SetHDRMetaData(t,s,d); }

} // namespace adrena