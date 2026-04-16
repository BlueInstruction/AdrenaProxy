#pragma once
#include <dxgi1_6.h>
#include <d3d11.h>
#include <d3d12.h>

namespace adrena {

class ProxySwapChain : public IDXGISwapChain4 {
public:
    ProxySwapChain(IDXGISwapChain1* real, IUnknown* device);
    ~ProxySwapChain();

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override;
    ULONG   STDMETHODCALLTYPE AddRef() override;
    ULONG   STDMETHODCALLTYPE Release() override;

    HRESULT STDMETHODCALLTYPE SetPrivateData(REFGUID, UINT, const void*) override;
    HRESULT STDMETHODCALLTYPE SetPrivateDataInterface(REFGUID, const IUnknown*) override;
    HRESULT STDMETHODCALLTYPE GetPrivateData(REFGUID, UINT*, void*) override;
    HRESULT STDMETHODCALLTYPE GetParent(REFIID, void**) override;
    HRESULT STDMETHODCALLTYPE GetDevice(REFIID, void**) override;

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

    HRESULT STDMETHODCALLTYPE SetSourceSize(UINT, UINT) override;
    HRESULT STDMETHODCALLTYPE GetSourceSize(UINT*, UINT*) override;
    HRESULT STDMETHODCALLTYPE SetMaximumFrameLatency(UINT) override;
    HRESULT STDMETHODCALLTYPE GetMaximumFrameLatency(UINT*) override;
    HANDLE  STDMETHODCALLTYPE GetFrameLatencyWaitableObject() override;
    HRESULT STDMETHODCALLTYPE SetMatrixTransform(const DXGI_MATRIX_3X2_F*) override;
    HRESULT STDMETHODCALLTYPE GetMatrixTransform(DXGI_MATRIX_3X2_F*) override;

    UINT    STDMETHODCALLTYPE GetCurrentBackBufferIndex() override;
    HRESULT STDMETHODCALLTYPE CheckColorSpaceSupport(DXGI_COLOR_SPACE_TYPE, UINT*) override;
    HRESULT STDMETHODCALLTYPE SetColorSpace1(DXGI_COLOR_SPACE_TYPE) override;
    HRESULT STDMETHODCALLTYPE ResizeBuffers1(UINT, UINT, UINT, DXGI_FORMAT, UINT,
                    const UINT*, IUnknown* const*) override;
    HRESULT STDMETHODCALLTYPE SetHDRMetaData(DXGI_HDR_METADATA_TYPE, UINT, void*) override;

private:
    HRESULT HookPresent(UINT SyncInterval, UINT Flags);
    void SetupResources();
    void TeardownResources();
    void ProcessSGSR11();
    void ProcessSGSR12();
    void RenderOverlay();

    // FIX: Use IDXGISwapChain4* so all methods are accessible
    IDXGISwapChain4*    m_real;
    ULONG               m_refCount = 1;
    bool                m_isD3D12 = false;
    bool                m_initialized = false;

    ID3D11Device*       m_d3d11Device = nullptr;
    ID3D11DeviceContext* m_d3d11Ctx = nullptr;
    ID3D12Device*       m_d3d12Device = nullptr;
    ID3D12CommandQueue* m_commandQueue = nullptr;
    ID3D12GraphicsCommandList* m_cmdList = nullptr;
    ID3D12CommandAllocator* m_cmdAlloc = nullptr;

    ID3D11Texture2D*    m_lowresRT11 = nullptr;
    UINT                m_renderWidth = 0, m_renderHeight = 0;
    UINT                m_displayWidth = 0, m_displayHeight = 0;
    DXGI_FORMAT         m_format = DXGI_FORMAT_UNKNOWN;
};

} // namespace adrena
