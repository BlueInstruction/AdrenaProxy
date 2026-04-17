#include "proxy_factory.h"
#include "proxy_swapchain.h"
#include <adrena_core/logger.h>

namespace adrena {

ProxyFactory::ProxyFactory(IDXGIFactory6* real) : m_real(real) {
    AD_LOG_I("ProxyFactory created");
}

ProxyFactory::~ProxyFactory() {
    if (m_real) m_real->Release();
}

HRESULT ProxyFactory::QueryInterface(REFIID riid, void** ppv) {
    if (!ppv) return E_POINTER;
    if (riid == __uuidof(IUnknown) || riid == __uuidof(IDXGIObject) ||
        riid == __uuidof(IDXGIFactory) || riid == __uuidof(IDXGIFactory1) ||
        riid == __uuidof(IDXGIFactory2) || riid == __uuidof(IDXGIFactory3) ||
        riid == __uuidof(IDXGIFactory4) || riid == __uuidof(IDXGIFactory5) ||
        riid == __uuidof(IDXGIFactory6)) {
        *ppv = this;
        AddRef();
        return S_OK;
    }
    return m_real->QueryInterface(riid, ppv);
}

ULONG ProxyFactory::AddRef() { return InterlockedIncrement(&m_ref); }

ULONG ProxyFactory::Release() {
    ULONG r = InterlockedDecrement(&m_ref);
    if (r == 0) delete this;
    return r;
}

#define PASS_THROUGH(name, ...) return m_real->name(__VA_ARGS__)

HRESULT ProxyFactory::SetPrivateData(REFGUID g, UINT s, const void* d) { PASS_THROUGH(SetPrivateData, g, s, d); }
HRESULT ProxyFactory::SetPrivateDataInterface(REFGUID g, const IUnknown* u) { PASS_THROUGH(SetPrivateDataInterface, g, u); }
HRESULT ProxyFactory::GetPrivateData(REFGUID g, UINT* s, void* d) { PASS_THROUGH(GetPrivateData, g, s, d); }
HRESULT ProxyFactory::GetParent(REFIID r, void** p) { PASS_THROUGH(GetParent, r, p); }
HRESULT ProxyFactory::EnumAdapters(UINT i, IDXGIAdapter** a) { PASS_THROUGH(EnumAdapters, i, a); }
HRESULT ProxyFactory::MakeWindowAssociation(HWND h, UINT f) { PASS_THROUGH(MakeWindowAssociation, h, f); }
HRESULT ProxyFactory::GetWindowAssociation(HWND* h) { PASS_THROUGH(GetWindowAssociation, h); }
HRESULT ProxyFactory::CreateSoftwareAdapter(UINT i, IDXGIAdapter** a) { PASS_THROUGH(CreateSoftwareAdapter, i, a); }
HRESULT ProxyFactory::EnumAdapters1(UINT i, IDXGIAdapter1** a) { PASS_THROUGH(EnumAdapters1, i, a); }
BOOL ProxyFactory::IsCurrent() { return m_real->IsCurrent(); }
BOOL ProxyFactory::IsWindowedStereoEnabled() { return m_real->IsWindowedStereoEnabled(); }
HRESULT ProxyFactory::GetSharedResourceAdapterLuid(HANDLE h, LUID* l) { PASS_THROUGH(GetSharedResourceAdapterLuid, h, l); }
HRESULT ProxyFactory::RegisterStereoStatusWindow(HWND h, UINT u, DWORD* d) { PASS_THROUGH(RegisterStereoStatusWindow, h, u, d); }
HRESULT ProxyFactory::RegisterStereoStatusEvent(HANDLE h, DWORD* d) { PASS_THROUGH(RegisterStereoStatusEvent, h, d); }
HRESULT ProxyFactory::UnregisterStereoStatus(DWORD d) { PASS_THROUGH(UnregisterStereoStatus, d); }
HRESULT ProxyFactory::RegisterOcclusionStatusWindow(HWND h, UINT u, DWORD* d) { PASS_THROUGH(RegisterOcclusionStatusWindow, h, u, d); }
HRESULT ProxyFactory::RegisterOcclusionStatusEvent(HANDLE h, DWORD* d) { PASS_THROUGH(RegisterOcclusionStatusEvent, h, d); }
HRESULT ProxyFactory::UnregisterOcclusionStatus(DWORD d) { PASS_THROUGH(UnregisterOcclusionStatus, d); }
UINT ProxyFactory::GetCreationFlags() { return m_real->GetCreationFlags(); }
HRESULT ProxyFactory::EnumAdapterByLuid(LUID l, REFIID r, void** p) { PASS_THROUGH(EnumAdapterByLuid, l, r, p); }
HRESULT ProxyFactory::EnumWarpAdapter(REFIID r, void** p) { PASS_THROUGH(EnumWarpAdapter, r, p); }
HRESULT ProxyFactory::CheckFeatureSupport(DXGI_FEATURE f, void* s, UINT sz) { PASS_THROUGH(CheckFeatureSupport, f, s, sz); }
HRESULT ProxyFactory::EnumAdapterByGpuPreference(UINT i, DXGI_GPU_PREFERENCE p, REFIID r, void** v) { PASS_THROUGH(EnumAdapterByGpuPreference, i, p, r, v); }

// ── SwapChain creation — wrap the result ──

HRESULT ProxyFactory::CreateSwapChain(IUnknown* device, DXGI_SWAP_CHAIN_DESC* desc, IDXGISwapChain** swapChain) {
    AD_LOG_I("CreateSwapChain called");
    HRESULT hr = m_real->CreateSwapChain(device, desc, swapChain);
    if (SUCCEEDED(hr) && swapChain && *swapChain) {
        IDXGISwapChain4* sc4 = nullptr;
        if (SUCCEEDED((*swapChain)->QueryInterface(__uuidof(IDXGISwapChain4), (void**)&sc4))) {
            (*swapChain)->Release();
            *swapChain = new ProxySwapChain(sc4, device);
            AD_LOG_I("SwapChain wrapped (CreateSwapChain)");
        }
    }
    return hr;
}

HRESULT ProxyFactory::CreateSwapChainForHwnd(IUnknown* device, HWND hwnd, const DXGI_SWAP_CHAIN_DESC1* desc,
    const DXGI_SWAP_CHAIN_FULLSCREEN_DESC* fs, IDXGIOutput* output, IDXGISwapChain1** swapChain) {
    AD_LOG_I("CreateSwapChainForHwnd called");
    HRESULT hr = m_real->CreateSwapChainForHwnd(device, hwnd, desc, fs, output, swapChain);
    if (SUCCEEDED(hr) && swapChain && *swapChain) {
        IDXGISwapChain4* sc4 = nullptr;
        if (SUCCEEDED((*swapChain)->QueryInterface(__uuidof(IDXGISwapChain4), (void**)&sc4))) {
            (*swapChain)->Release();
            *swapChain = new ProxySwapChain(sc4, device);
            AD_LOG_I("SwapChain wrapped (ForHwnd)");
        }
    }
    return hr;
}

HRESULT ProxyFactory::CreateSwapChainForCoreWindow(IUnknown* device, IUnknown* window,
    const DXGI_SWAP_CHAIN_DESC1* desc, IDXGIOutput* output, IDXGISwapChain1** swapChain) {
    HRESULT hr = m_real->CreateSwapChainForCoreWindow(device, window, desc, output, swapChain);
    if (SUCCEEDED(hr) && swapChain && *swapChain) {
        IDXGISwapChain4* sc4 = nullptr;
        if (SUCCEEDED((*swapChain)->QueryInterface(__uuidof(IDXGISwapChain4), (void**)&sc4))) {
            (*swapChain)->Release();
            *swapChain = new ProxySwapChain(sc4, device);
        }
    }
    return hr;
}

HRESULT ProxyFactory::CreateSwapChainForComposition(IUnknown* device,
    const DXGI_SWAP_CHAIN_DESC1* desc, IDXGIOutput* output, IDXGISwapChain1** swapChain) {
    HRESULT hr = m_real->CreateSwapChainForComposition(device, desc, output, swapChain);
    if (SUCCEEDED(hr) && swapChain && *swapChain) {
        IDXGISwapChain4* sc4 = nullptr;
        if (SUCCEEDED((*swapChain)->QueryInterface(__uuidof(IDXGISwapChain4), (void**)&sc4))) {
            (*swapChain)->Release();
            *swapChain = new ProxySwapChain(sc4, device);
        }
    }
    return hr;
}

} // namespace adrena