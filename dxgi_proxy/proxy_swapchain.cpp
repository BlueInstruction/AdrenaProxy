#include "proxy_swapchain.h"
#include <adrena_core/config.h>
#include <adrena_core/logger.h>
#include <adrena_core/shared_state.h>
#include <adrena_core/sgsr1_pass.h>
#include <adrena_core/overlay_menu.h>

#include <d3d12.h>

namespace adrena {

WNDPROC ProxySwapChain::s_origWndProc = nullptr;
ProxySwapChain* ProxySwapChain::s_instance = nullptr;

// ────────────────────────────────────────────────────────
// Construction / Destruction
// ────────────────────────────────────────────────────────

ProxySwapChain::ProxySwapChain(IDXGISwapChain4* real, IUnknown* device)
    : m_real(real) {

    // Detect D3D12
    ID3D12CommandQueue* cq = nullptr;
    if (SUCCEEDED(device->QueryInterface(__uuidof(ID3D12CommandQueue), (void**)&cq))) {
        m_isD3D12  = true;
        m_cmdQueue = cq;
        m_cmdQueue->GetDevice(__uuidof(ID3D12Device), (void**)&m_device);
        AD_LOG_I("SwapChain: D3D12 detected");
    } else {
        AD_LOG_I("SwapChain: D3D11 or unknown API");
    }

    // Read SharedState for DLSS presence
    SharedState* ss = GetSharedState();
    if (ss) {
        SharedStateLock l(&ss->lock);
        m_dlssActive = ss->sgsr_active;
    }

    // Get swap chain dimensions
    DXGI_SWAP_CHAIN_DESC desc{};
    if (SUCCEEDED(m_real->GetDesc(&desc))) {
        m_width  = desc.BufferDesc.Width;
        m_height = desc.BufferDesc.Height;
        m_format = desc.BufferDesc.Format;
    }

    s_instance = this;
    AD_LOG_I("ProxySwapChain created: %ux%u fmt=%u DLSS=%d",
             m_width, m_height, (uint32_t)m_format, m_dlssActive);
}

ProxySwapChain::~ProxySwapChain() {
    // Wait for any in-flight SGSR GPU work before releasing
    if (m_sgsrFence && m_sgsrFenceVal > 0 && m_sgsrFence->GetCompletedValue() < m_sgsrFenceVal) {
        m_sgsrFence->SetEventOnCompletion(m_sgsrFenceVal, m_sgsrFenceEvent);
        WaitForSingleObject(m_sgsrFenceEvent, INFINITE);
    }
    if (m_sgsrCmdList)    { m_sgsrCmdList->Release();    m_sgsrCmdList = nullptr; }
    if (m_sgsrCmdAlloc)   { m_sgsrCmdAlloc->Release();   m_sgsrCmdAlloc = nullptr; }
    if (m_sgsrFence)      { m_sgsrFence->Release();      m_sgsrFence = nullptr; }
    if (m_sgsrFenceEvent) { CloseHandle(m_sgsrFenceEvent); m_sgsrFenceEvent = nullptr; }
    if (m_sgsr)    { delete m_sgsr;    m_sgsr    = nullptr; }
    if (m_overlay) { delete m_overlay;  m_overlay = nullptr; }
    if (m_device)   m_device->Release();
    if (m_cmdQueue) m_cmdQueue->Release();
    if (m_real)     m_real->Release();
    s_instance = nullptr;
    AD_LOG_I("ProxySwapChain destroyed");
}

// ────────────────────────────────────────────────────────
// IUnknown
// ────────────────────────────────────────────────────────

HRESULT ProxySwapChain::QueryInterface(REFIID riid, void** ppv) {
    if (!ppv) return E_POINTER;
    if (riid == __uuidof(IUnknown)       ||
        riid == __uuidof(IDXGISwapChain) ||
        riid == __uuidof(IDXGISwapChain1)||
        riid == __uuidof(IDXGISwapChain2)||
        riid == __uuidof(IDXGISwapChain3)||
        riid == __uuidof(IDXGISwapChain4)) {
        *ppv = this; AddRef(); return S_OK;
    }
    return m_real->QueryInterface(riid, ppv);
}

ULONG ProxySwapChain::AddRef()  { return InterlockedIncrement(&m_ref); }
ULONG ProxySwapChain::Release() { ULONG r = InterlockedDecrement(&m_ref); if (r == 0) delete this; return r; }

// ────────────────────────────────────────────────────────
// Pass-through DXGI methods
// ────────────────────────────────────────────────────────

#define PT(name, ...) return m_real->name(__VA_ARGS__)

HRESULT ProxySwapChain::SetPrivateData(REFGUID g, UINT s, const void* d)
    { PT(SetPrivateData, g, s, d); }
HRESULT ProxySwapChain::SetPrivateDataInterface(REFGUID g, const IUnknown* u)
    { PT(SetPrivateDataInterface, g, u); }
HRESULT ProxySwapChain::GetPrivateData(REFGUID g, UINT* s, void* d)
    { PT(GetPrivateData, g, s, d); }
HRESULT ProxySwapChain::GetParent(REFIID r, void** p)
    { PT(GetParent, r, p); }
HRESULT ProxySwapChain::GetDevice(REFIID r, void** p)
    { PT(GetDevice, r, p); }

// ── GetBuffer: PASS-THROUGH ──
// When dlss.dll is active, the game already renders at lower resolution
// because DLSS_GetOptimalSettings told it to. No redirect needed.
HRESULT ProxySwapChain::GetBuffer(UINT i, REFIID r, void** p)
    { return m_real->GetBuffer(i, r, p); }

HRESULT ProxySwapChain::SetFullscreenState(BOOL f, IDXGIOutput* o)
    { PT(SetFullscreenState, f, o); }
HRESULT ProxySwapChain::GetFullscreenState(BOOL* f, IDXGIOutput** o)
    { PT(GetFullscreenState, f, o); }
HRESULT ProxySwapChain::GetDesc(DXGI_SWAP_CHAIN_DESC* d)
    { PT(GetDesc, d); }
HRESULT ProxySwapChain::ResizeTarget(const DXGI_MODE_DESC* d)
    { PT(ResizeTarget, d); }
HRESULT ProxySwapChain::GetContainingOutput(IDXGIOutput** o)
    { PT(GetContainingOutput, o); }
HRESULT ProxySwapChain::GetFrameStatistics(DXGI_FRAME_STATISTICS* s)
    { PT(GetFrameStatistics, s); }
HRESULT ProxySwapChain::GetLastPresentCount(UINT* c)
    { PT(GetLastPresentCount, c); }
HRESULT ProxySwapChain::GetDesc1(DXGI_SWAP_CHAIN_DESC1* d)
    { PT(GetDesc1, d); }
HRESULT ProxySwapChain::GetFullscreenDesc(DXGI_SWAP_CHAIN_FULLSCREEN_DESC* d)
    { PT(GetFullscreenDesc, d); }
HRESULT ProxySwapChain::GetHwnd(HWND* h)
    { PT(GetHwnd, h); }
HRESULT ProxySwapChain::GetCoreWindow(REFIID r, void** p)
    { PT(GetCoreWindow, r, p); }
BOOL    ProxySwapChain::IsTemporaryMonoSupported()
    { return m_real->IsTemporaryMonoSupported(); }
HRESULT ProxySwapChain::GetRestrictToOutput(IDXGIOutput** o)
    { PT(GetRestrictToOutput, o); }
HRESULT ProxySwapChain::SetBackgroundColor(const DXGI_RGBA* c)
    { PT(SetBackgroundColor, c); }
HRESULT ProxySwapChain::GetBackgroundColor(DXGI_RGBA* c)
    { PT(GetBackgroundColor, c); }
HRESULT ProxySwapChain::SetRotation(DXGI_MODE_ROTATION r)
    { PT(SetRotation, r); }
HRESULT ProxySwapChain::GetRotation(DXGI_MODE_ROTATION* r)
    { PT(GetRotation, r); }
HRESULT ProxySwapChain::GetMaximumFrameLatency(UINT* l)
    { PT(GetMaximumFrameLatency, l); }
HANDLE  ProxySwapChain::GetFrameLatencyWaitableObject()
    { return m_real->GetFrameLatencyWaitableObject(); }
HRESULT ProxySwapChain::SetMatrixTransform(const DXGI_MATRIX_3X2_F* m)
    { PT(SetMatrixTransform, m); }
HRESULT ProxySwapChain::GetMatrixTransform(DXGI_MATRIX_3X2_F* m)
    { PT(GetMatrixTransform, m); }
UINT    ProxySwapChain::GetCurrentBackBufferIndex()
    { return m_real->GetCurrentBackBufferIndex(); }
HRESULT ProxySwapChain::CheckColorSpaceSupport(DXGI_COLOR_SPACE_TYPE t, UINT* s)
    { PT(CheckColorSpaceSupport, t, s); }
HRESULT ProxySwapChain::SetColorSpace1(DXGI_COLOR_SPACE_TYPE t)
    { PT(SetColorSpace1, t); }
HRESULT ProxySwapChain::SetHDRMetaData(DXGI_HDR_METADATA_TYPE t, UINT s, void* d)
    { PT(SetHDRMetaData, t, s, d); }

HRESULT ProxySwapChain::SetSourceSize(UINT w, UINT h)
    { PT(SetSourceSize, w, h); }
HRESULT ProxySwapChain::GetSourceSize(UINT* w, UINT* h)
    { PT(GetSourceSize, w, h); }

HRESULT ProxySwapChain::SetMaximumFrameLatency(UINT l) {
    HRESULT hr = m_real->SetMaximumFrameLatency(l);
    if (hr == 0x887A0001) // DXGI_ERROR_NOT_CURRENTLY_AVAILABLE — normal on VKD3D
        AD_LOG_W("SetMaximumFrameLatency(%u) not supported (VKD3D normal)", l);
    else
        AD_LOG_I("SetMaximumFrameLatency(%u) hr=0x%08X", l, hr);
    return hr;
}

HRESULT ProxySwapChain::ResizeBuffers(UINT count, UINT w, UINT h, DXGI_FORMAT fmt, UINT flags) {
    AD_LOG_I("ResizeBuffers: %ux%u fmt=%u", w, h, (uint32_t)fmt);
    m_width  = w;
    m_height = h;
    m_format = fmt;
    SharedState* ss = GetSharedState();
    if (ss) { SharedStateLock l(&ss->lock); ss->display_width = w; ss->display_height = h; }
    if (m_sgsr && !m_dlssActive) {
        Config& cfg = GetConfig();
        uint32_t rw = (uint32_t)(w * cfg.render_scale);
        uint32_t rh = (uint32_t)(h * cfg.render_scale);
        m_sgsr->Resize(rw, rh, w, h);
    }
    return m_real->ResizeBuffers(count, w, h, fmt, flags);
}

HRESULT ProxySwapChain::ResizeBuffers1(UINT c, UINT w, UINT h, DXGI_FORMAT f, UINT fl,
    const UINT* n, IUnknown* const* o) {
    m_width = w; m_height = h;
    return m_real->ResizeBuffers1(c, w, h, f, fl, n, o);
}

HRESULT ProxySwapChain::Present1(UINT sync, UINT flags, const DXGI_PRESENT_PARAMETERS* params) {
    return PresentImpl(sync, flags);
}

// ────────────────────────────────────────────────────────
// Core Present implementation
// ────────────────────────────────────────────────────────

void ProxySwapChain::EnsureD3D12Infrastructure() {
    if (!m_isD3D12 || !m_device) return;

    // Create fallback SGSR (Path B — non-DLSS games)
    if (!m_sgsr && !m_dlssActive) {
        m_sgsr = new SGSR1Pass();
        Config& cfg = GetConfig();
        uint32_t rw = (uint32_t)(m_width  * cfg.render_scale);
        uint32_t rh = (uint32_t)(m_height * cfg.render_scale);
        if (rw < 1) rw = 1;
        if (rh < 1) rh = 1;
        m_sgsr->Init(m_device, m_format, rw, rh, m_width, m_height);
        AD_LOG_I("Fallback SGSR1 initialized (%ux%u -> %ux%u)", rw, rh, m_width, m_height);
    }

    // Create persistent D3D12 resources for SGSR compute
    if (!m_sgsrCmdAlloc && m_device) {
        m_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
            IID_ID3D12CommandAllocator, (void**)&m_sgsrCmdAlloc);
        if (m_sgsrCmdAlloc) {
            m_device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT,
                m_sgsrCmdAlloc, nullptr, IID_ID3D12GraphicsCommandList, (void**)&m_sgsrCmdList);
            if (m_sgsrCmdList) m_sgsrCmdList->Close();
        }
        m_device->CreateFence(0, D3D12_FENCE_FLAG_NONE,
            IID_ID3D12Fence, (void**)&m_sgsrFence);
        m_sgsrFenceEvent = CreateEventA(nullptr, FALSE, FALSE, nullptr);
    }

    // Create overlay
    if (!m_overlay) {
        m_overlay = new OverlayMenu();
        if (!m_wndHooked) {
            IDXGISwapChain1* sc1 = nullptr;
            if (SUCCEEDED(m_real->QueryInterface(__uuidof(IDXGISwapChain1), (void**)&sc1))) {
                sc1->GetHwnd(&m_hwnd);
                sc1->Release();
            }
            if (m_hwnd) {
                m_overlay->InitWin32(m_hwnd);
                s_origWndProc = (WNDPROC)SetWindowLongPtrA(m_hwnd, GWLP_WNDPROC, (LONG_PTR)StaticWndProc);
                m_wndHooked = true;
                AD_LOG_I("WndProc hooked (HWND=%p)", m_hwnd);
            }
        }
        if (m_device) m_overlay->InitDX12(m_device, m_format);
    }
}

void ProxySwapChain::CheckConfigChange() {
    // Throttle config checks — only every 60 frames
    if (m_frameCount - m_lastConfigCheck < 60) return;
    m_lastConfigCheck = m_frameCount;

    Config& cfg = GetConfig();
    SharedState* ss = GetSharedState();
    if (ss) {
        SharedStateLock l(&ss->lock);
        m_dlssActive = ss->sgsr_active;

        if (!m_dlssActive) {
            int curFg = ss->fg_mode;
            float curScale = ss->render_scale;
            if (curFg != m_lastFgMode || curScale != m_lastScale) {
                if (m_sgsr && m_width > 0 && m_height > 0) {
                    uint32_t rw = (uint32_t)(m_width  * curScale);
                    uint32_t rh = (uint32_t)(m_height * curScale);
                    if (rw < 1) rw = 1;
                    if (rh < 1) rh = 1;
                    m_sgsr->Resize(rw, rh, m_width, m_height);
                    AD_LOG_I("Config changed: scale %.2f fg x%d", curScale, curFg);
                }
                m_lastFgMode = curFg;
                m_lastScale  = curScale;
            }
        }
    }
}

HRESULT ProxySwapChain::PresentImpl(UINT SyncInterval, UINT Flags) {
    m_frameCount++;

    // ── Read SharedState once ──
    bool dlssActive = m_dlssActive;
    bool sgsrEnabled = false;
    bool overlayVisible = false;
    int  fgMode = 1;
    bool fpsDisplay = false;

    SharedState* ss = GetSharedState();
    if (ss) {
        SharedStateLock l(&ss->lock);
        dlssActive    = ss->sgsr_active;
        sgsrEnabled   = ss->sgsr_enabled;
        fgMode        = ss->fg_mode;
        overlayVisible = ss->overlay_visible;
        fpsDisplay    = ss->fps_display;
    }

    Config& cfg = GetConfig();

    // ── Early exit: if everything is OFF, pass through with zero overhead ──
    bool needSGSR   = (!dlssActive && sgsrEnabled && m_isD3D12);
    bool needFG     = (fgMode > 1);
    bool needOverlay = (overlayVisible || fpsDisplay);

    if (!needSGSR && !needFG && !needOverlay && !cfg.overlay_enabled) {
        return m_real->Present(SyncInterval, Flags);
    }

    // ── Ensure infrastructure on first active frame ──
    EnsureD3D12Infrastructure();

    // ── Throttled config check ──
    CheckConfigChange();

    // ── Path B: Fallback SGSR sharpening (non-DLSS games) ──
    if (needSGSR && m_sgsr && m_sgsr->IsInitialized() && m_device
        && m_sgsrCmdAlloc && m_sgsrCmdList && m_sgsrFence) {

        // Wait for previous SGSR frame to complete before reusing allocator
        if (m_sgsrFenceVal > 0 && m_sgsrFence->GetCompletedValue() < m_sgsrFenceVal) {
            m_sgsrFence->SetEventOnCompletion(m_sgsrFenceVal, m_sgsrFenceEvent);
            WaitForSingleObject(m_sgsrFenceEvent, INFINITE);
        }

        ID3D12Resource* backbuffer = nullptr;
        if (SUCCEEDED(m_real->GetBuffer(m_real->GetCurrentBackBufferIndex(),
            IID_ID3D12Resource, (void**)&backbuffer))) {

            m_sgsrCmdAlloc->Reset();
            m_sgsrCmdList->Reset(m_sgsrCmdAlloc, nullptr);

            float scale = ss ? ss->render_scale : cfg.render_scale;
            uint32_t rw = (uint32_t)(m_width * scale);
            uint32_t rh = (uint32_t)(m_height * scale);
            if (rw < 1) rw = 1;
            if (rh < 1) rh = 1;

            SGSRParams params{};
            params.color   = backbuffer;
            params.output  = backbuffer; // In-place: sharpening only
            params.sharpness = ss ? ss->sharpness : cfg.sharpness;
            params.renderWidth  = rw;
            params.renderHeight = rh;
            params.displayWidth  = m_width;
            params.displayHeight = m_height;

            m_sgsr->Execute(m_sgsrCmdList, params);
            m_sgsrCmdList->Close();

            ID3D12CommandList* lists[] = { m_sgsrCmdList };
            m_cmdQueue->ExecuteCommandLists(1, lists);

            // Signal fence so next frame waits for completion
            m_sgsrFenceVal++;
            m_cmdQueue->Signal(m_sgsrFence, m_sgsrFenceVal);

            backbuffer->Release();
        }
    }

    // ── Frame Generation: Pure Extra Present ──
    // No compute shader, no GPU work — just extra Present(0,0) calls.
    // This stimulates the Vulkan queue on Turnip/Adreno and boosts FPS
    // without changing the backbuffer index (fixes ImGui flickering).
    int extraPresents = fgMode - 1; // fgMode=2 → 1 extra, fgMode=4 → 3 extra
    if (needFG && extraPresents > 0) {
        for (int i = 0; i < extraPresents; i++) {
            m_real->Present(0, 0);
        }
    }

    // ── ImGui Overlay (drawn ONCE, on the final real backbuffer) ──
    // Render whenever needOverlay is true (FPS counter or menu visible).
    // OverlayMenu::Render internally handles m_visible for the menu,
    // and RenderHUD always draws the FPS counter when fps_display is on.
    if (needOverlay && m_overlay && m_cmdQueue) {
        ID3D12Resource* bb = nullptr;
        if (SUCCEEDED(m_real->GetBuffer(m_real->GetCurrentBackBufferIndex(),
            IID_ID3D12Resource, (void**)&bb))) {
            m_overlay->Render(m_cmdQueue, bb, m_width, m_height);
            bb->Release();
        }
    }

    // ── Final real Present ──
    return m_real->Present(SyncInterval, Flags);
}

// ── Present (standard) ──
HRESULT ProxySwapChain::Present(UINT SyncInterval, UINT Flags) {
    return PresentImpl(SyncInterval, Flags);
}

// ────────────────────────────────────────────────────────
// WndProc hook for overlay toggle
// ────────────────────────────────────────────────────────

LRESULT CALLBACK ProxySwapChain::StaticWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_KEYDOWN) {
        Config& cfg = GetConfig();
        if (wp == cfg.toggle_key && s_instance && s_instance->m_overlay) {
            s_instance->m_overlay->Toggle();
            return 0;
        }
    }
    if (s_origWndProc)
        return CallWindowProcA(s_origWndProc, hwnd, msg, wp, lp);
    return DefWindowProcA(hwnd, msg, wp, lp);
}

} // namespace adrena