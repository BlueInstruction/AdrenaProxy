#pragma once
#include <dxgi1_6.h>
#include <d3d12.h>
#include <cstdint>

namespace adrena {
class SGSR1Pass;
class OverlayMenu;
}

namespace adrena {

class ProxySwapChain : public IDXGISwapChain4 {
public:
    ProxySwapChain(IDXGISwapChain4* real, IUnknown* device);
    ~ProxySwapChain();

    // IUnknown
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override;
    ULONG   STDMETHODCALLTYPE AddRef() override;
    ULONG   STDMETHODCALLTYPE Release() override;

    // IDXGIObject
    HRESULT STDMETHODCALLTYPE SetPrivateData(REFGUID, UINT, const void*) override;
    HRESULT STDMETHODCALLTYPE SetPrivateDataInterface(REFGUID, const IUnknown*) override;
    HRESULT STDMETHODCALLTYPE GetPrivateData(REFGUID, UINT*, void*) override;
    HRESULT STDMETHODCALLTYPE GetParent(REFIID, void**) override;

    // IDXGISwapChain
    HRESULT STDMETHODCALLTYPE Present(UINT, UINT) override;
    HRESULT STDMETHODCALLTYPE GetBuffer(UINT, REFIID, void**) override;
    HRESULT STDMETHODCALLTYPE SetFullscreenState(BOOL, IDXGIOutput*) override;
    HRESULT STDMETHODCALLTYPE GetFullscreenState(BOOL*, IDXGIOutput**) override;
    HRESULT STDMETHODCALLTYPE GetDesc(DXGI_SWAP_CHAIN_DESC*) override;
    HRESULT STDMETHODCALLTYPE ResizeBuffers(UINT, UINT, UINT, DXGI_FORMAT, UINT) override;
    HRESULT STDMETHODCALLTYPE ResizeTarget(const DXGI_MODE_DESC*) override;
    HRESULT STDMETHODCALLTYPE GetContainingOutput(IDXGIOutput**) override;
    HRESULT STDMETHODCALLTYPE GetFrameStatistics(DXGI_FRAME_STATISTICS*) override;
    HRESULT STDMETHODCALLTYPE GetLastPresentCount(UINT*) override;

    // IDXGISwapChain1
    HRESULT STDMETHODCALLTYPE GetDesc1(DXGI_SWAP_CHAIN_DESC1*) override;
    HRESULT STDMETHODCALLTYPE GetFullscreenDesc(DXGI_SWAP_CHAIN_FULLSCREEN_DESC*) override;
    HRESULT STDMETHODCALLTYPE GetHwnd(HWND*) override;
    HRESULT STDMETHODCALLTYPE GetCoreWindow(REFIID, void**) override;
    HRESULT STDMETHODCALLTYPE Present1(UINT, UINT, const DXGI_PRESENT_PARAMETERS*) override;
    BOOL    STDMETHODCALLTYPE IsTemporaryMonoSupported() override;
    HRESULT STDMETHODCALLTYPE GetRestrictToOutput(IDXGIOutput**) override;
    HRESULT STDMETHODCALLTYPE SetBackgroundColor(const DXGI_RGBA*) override;
    HRESULT STDMETHODCALLTYPE GetBackgroundColor(DXGI_RGBA*) override;
    HRESULT STDMETHODCALLTYPE SetRotation(DXGI_MODE_ROTATION) override;
    HRESULT STDMETHODCALLTYPE GetRotation(DXGI_MODE_ROTATION*) override;

    // IDXGISwapChain2
    HRESULT STDMETHODCALLTYPE SetSourceSize(UINT, UINT) override;
    HRESULT STDMETHODCALLTYPE GetSourceSize(UINT*, UINT*) override;
    HRESULT STDMETHODCALLTYPE SetMaximumFrameLatency(UINT) override;
    HRESULT STDMETHODCALLTYPE GetMaximumFrameLatency(UINT*) override;
    HANDLE  STDMETHODCALLTYPE GetFrameLatencyWaitableObject() override;
    HRESULT STDMETHODCALLTYPE SetMatrixTransform(const DXGI_MATRIX_3X2_F*) override;
    HRESULT STDMETHODCALLTYPE GetMatrixTransform(DXGI_MATRIX_3X2_F*) override;

    // IDXGISwapChain3
    UINT    STDMETHODCALLTYPE GetCurrentBackBufferIndex() override;
    HRESULT STDMETHODCALLTYPE CheckColorSpaceSupport(DXGI_COLOR_SPACE_TYPE, UINT*) override;
    HRESULT STDMETHODCALLTYPE SetColorSpace1(DXGI_COLOR_SPACE_TYPE) override;
    HRESULT STDMETHODCALLTYPE ResizeBuffers1(UINT, UINT, UINT, DXGI_FORMAT, UINT, const UINT*, IUnknown* const*) override;

    // IDXGISwapChain4
    HRESULT STDMETHODCALLTYPE SetHDRMetaData(DXGI_HDR_METADATA_TYPE, UINT, void*) override;

private:
    void EnsureD3D12Infrastructure();
    void CheckConfigChange();
    HRESULT PresentImpl(UINT SyncInterval, UINT Flags);

    IDXGISwapChain4*  m_real;
    ID3D12Device*     m_device     = nullptr;
    ID3D12CommandQueue* m_cmdQueue = nullptr;
    ULONG             m_ref        = 1;
    bool              m_isD3D12    = false;
    bool              m_dlssActive = false;  // Set from SharedState
    uint64_t          m_frameCount = 0;

    SGSR1Pass*        m_sgsr       = nullptr;  // Fallback sharpening
    OverlayMenu*      m_overlay    = nullptr;
    HWND              m_hwnd       = nullptr;
    bool              m_wndHooked  = false;
    uint32_t          m_width      = 0;
    uint32_t          m_height     = 0;
    DXGI_FORMAT       m_format     = DXGI_FORMAT_UNKNOWN;

    // Config change detection
    uint64_t          m_lastConfigCheck = 0;
    float             m_lastScale       = 0.0f;
    int               m_lastFgMode      = 0;

    static LRESULT CALLBACK StaticWndProc(HWND, UINT, WPARAM, LPARAM);
    static WNDPROC s_origWndProc;
    static ProxySwapChain* s_instance;
};

} // namespace adrena