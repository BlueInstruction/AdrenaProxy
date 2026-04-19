#pragma once
#include <dxgi1_6.h>

namespace adrena {

class ProxyFactory : public IDXGIFactory6 {
public:
    ProxyFactory(IDXGIFactory6* real);
    ~ProxyFactory();

    // IUnknown
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override;
    ULONG   STDMETHODCALLTYPE AddRef() override;
    ULONG   STDMETHODCALLTYPE Release() override;

    // IDXGIObject
    HRESULT STDMETHODCALLTYPE SetPrivateData(REFGUID, UINT, const void*) override;
    HRESULT STDMETHODCALLTYPE SetPrivateDataInterface(REFGUID, const IUnknown*) override;
    HRESULT STDMETHODCALLTYPE GetPrivateData(REFGUID, UINT*, void*) override;
    HRESULT STDMETHODCALLTYPE GetParent(REFIID, void**) override;

    // IDXGIFactory
    HRESULT STDMETHODCALLTYPE EnumAdapters(UINT, IDXGIAdapter**) override;
    HRESULT STDMETHODCALLTYPE MakeWindowAssociation(HWND, UINT) override;
    HRESULT STDMETHODCALLTYPE GetWindowAssociation(HWND*) override;
    HRESULT STDMETHODCALLTYPE CreateSwapChain(IUnknown*, DXGI_SWAP_CHAIN_DESC*, IDXGISwapChain**) override;
    HRESULT STDMETHODCALLTYPE CreateSoftwareAdapter(HMODULE, IDXGIAdapter**) override;

    // IDXGIFactory1
    HRESULT STDMETHODCALLTYPE EnumAdapters1(UINT, IDXGIAdapter1**) override;
    BOOL    STDMETHODCALLTYPE IsCurrent() override;

    // IDXGIFactory2
    BOOL    STDMETHODCALLTYPE IsWindowedStereoEnabled() override;
    HRESULT STDMETHODCALLTYPE CreateSwapChainForHwnd(IUnknown*, HWND, const DXGI_SWAP_CHAIN_DESC1*,
        const DXGI_SWAP_CHAIN_FULLSCREEN_DESC*, IDXGIOutput*, IDXGISwapChain1**) override;
    HRESULT STDMETHODCALLTYPE CreateSwapChainForCoreWindow(IUnknown*, IUnknown*,
        const DXGI_SWAP_CHAIN_DESC1*, IDXGIOutput*, IDXGISwapChain1**) override;
    HRESULT STDMETHODCALLTYPE GetSharedResourceAdapterLuid(HANDLE, LUID*) override;
    HRESULT STDMETHODCALLTYPE RegisterStereoStatusWindow(HWND, UINT, DWORD*) override;
    HRESULT STDMETHODCALLTYPE RegisterStereoStatusEvent(HANDLE, DWORD*) override;
    void    STDMETHODCALLTYPE UnregisterStereoStatus(DWORD) override;
    HRESULT STDMETHODCALLTYPE RegisterOcclusionStatusWindow(HWND, UINT, DWORD*) override;
    HRESULT STDMETHODCALLTYPE RegisterOcclusionStatusEvent(HANDLE, DWORD*) override;
    void    STDMETHODCALLTYPE UnregisterOcclusionStatus(DWORD) override;
    HRESULT STDMETHODCALLTYPE CreateSwapChainForComposition(IUnknown*,
        const DXGI_SWAP_CHAIN_DESC1*, IDXGIOutput*, IDXGISwapChain1**) override;

    // IDXGIFactory3
    UINT    STDMETHODCALLTYPE GetCreationFlags() override;

    // IDXGIFactory4
    HRESULT STDMETHODCALLTYPE EnumAdapterByLuid(LUID, REFIID, void**) override;
    HRESULT STDMETHODCALLTYPE EnumWarpAdapter(REFIID, void**) override;

    // IDXGIFactory5
    HRESULT STDMETHODCALLTYPE CheckFeatureSupport(DXGI_FEATURE, void*, UINT) override;

    // IDXGIFactory6
    HRESULT STDMETHODCALLTYPE EnumAdapterByGpuPreference(UINT, DXGI_GPU_PREFERENCE, REFIID, void**) override;

private:
    IDXGIFactory6* m_real;
    ULONG m_ref = 1;
};

} // namespace adrena