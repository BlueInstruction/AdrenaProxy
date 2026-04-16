#include "proxy_factory.h"
#include "logger.h"

namespace adrena {

ProxyFactory::ProxyFactory(IDXGIFactory6* real) : m_real(real) {}
ProxyFactory::~ProxyFactory() { if (m_real) m_real->Release(); }

HRESULT ProxyFactory::QueryInterface(REFIID riid, void** ppv) {
    if (!ppv) return E_POINTER;
    if (riid == __uuidof(IUnknown) || riid == __uuidof(IDXGIObject) || riid == __uuidof(IDXGIFactory) ||
        riid == __uuidof(IDXGIFactory1) || riid == __uuidof(IDXGIFactory2) || riid == __uuidof(IDXGIFactory3) ||
        riid == __uuidof(IDXGIFactory4) || riid == __uuidof(IDXGIFactory5) || riid == __uuidof(IDXGIFactory6)) {
        *ppv = this; AddRef(); return S_OK;
    }
    return m_real->QueryInterface(riid, ppv);
}
ULONG ProxyFactory::AddRef() { return InterlockedIncrement(&m_refCount); }
ULONG ProxyFactory::Release() { ULONG c = InterlockedDecrement(&m_refCount); if (c == 0) delete this; return c; }

HRESULT ProxyFactory::SetPrivateData(REFGUID n, UINT d, const void* p) { return m_real->SetPrivateData(n,d,p); }
HRESULT ProxyFactory::SetPrivateDataInterface(REFGUID n, const IUnknown* p) { return m_real->SetPrivateDataInterface(n,p); }
HRESULT ProxyFactory::GetPrivateData(REFGUID n, UINT* s, void* p) { return m_real->GetPrivateData(n,s,p); }
HRESULT ProxyFactory::GetParent(REFIID r, void** p) { return m_real->GetParent(r,p); }
HRESULT ProxyFactory::EnumAdapters(UINT i, IDXGIAdapter** a) { return m_real->EnumAdapters(i,a); }
HRESULT ProxyFactory::MakeWindowAssociation(HWND h, UINT f) { return m_real->MakeWindowAssociation(h,f); }
HRESULT ProxyFactory::GetWindowAssociation(HWND* h) { return m_real->GetWindowAssociation(h); }

// FIX: HMODULE instead of UINT
HRESULT ProxyFactory::CreateSoftwareAdapter(HMODULE h, IDXGIAdapter** a) { return m_real->CreateSoftwareAdapter(h,a); }

HRESULT ProxyFactory::CreateSwapChain(IUnknown* dev, DXGI_SWAP_CHAIN_DESC* desc, IDXGISwapChain** sc) {
    IDXGISwapChain1* sc1 = nullptr;
    DXGI_SWAP_CHAIN_DESC1 desc1 = {};
    desc1.Width = desc->BufferDesc.Width; desc1.Height = desc->BufferDesc.Height;
    desc1.Format = desc->BufferDesc.Format; desc1.Stereo = FALSE;
    desc1.SampleDesc = desc->SampleDesc; desc1.BufferUsage = desc->BufferUsage;
    desc1.BufferCount = desc->BufferCount; desc1.Scaling = DXGI_SCALING_NONE;
    desc1.SwapEffect = desc->SwapEffect; desc1.AlphaMode = DXGI_ALPHA_MODE_UNSPECIFIED; desc1.Flags = desc->Flags;
    HRESULT hr = m_real->CreateSwapChainForHwnd(dev, desc->OutputWindow, &desc1, nullptr, nullptr, &sc1);
    if (SUCCEEDED(hr) && sc1 && sc) { *sc = sc1; return S_OK; }
    return hr;
}

HRESULT ProxyFactory::EnumAdapters1(UINT i, IDXGIAdapter1** a) { return m_real->EnumAdapters1(i,a); }
BOOL ProxyFactory::IsCurrent() { return m_real->IsCurrent(); }
BOOL ProxyFactory::IsWindowedStereoEnabled() { return m_real->IsWindowedStereoEnabled(); }

HRESULT ProxyFactory::CreateSwapChainForHwnd(IUnknown* dev, HWND hwnd, const DXGI_SWAP_CHAIN_DESC1* desc,
    const DXGI_SWAP_CHAIN_FULLSCREEN_DESC* fsDesc, IDXGIOutput* output, IDXGISwapChain1** sc) {
    IDXGISwapChain1* realSC = nullptr;
    HRESULT hr = m_real->CreateSwapChainForHwnd(dev, hwnd, desc, fsDesc, output, &realSC);
    if (SUCCEEDED(hr)) return WrapSwapChain(dev, realSC, sc);
    return hr;
}

HRESULT ProxyFactory::CreateSwapChainForCoreWindow(IUnknown* dev, IUnknown* wnd, const DXGI_SWAP_CHAIN_DESC1* desc,
    IDXGIOutput* out, IDXGISwapChain1** sc) {
    IDXGISwapChain1* realSC = nullptr;
    HRESULT hr = m_real->CreateSwapChainForCoreWindow(dev, wnd, desc, out, &realSC);
    if (SUCCEEDED(hr)) return WrapSwapChain(dev, realSC, sc);
    return hr;
}

HRESULT ProxyFactory::GetSharedResourceAdapterLuid(HANDLE h, LUID* l) { return m_real->GetSharedResourceAdapterLuid(h,l); }
HRESULT ProxyFactory::RegisterStereoStatusWindow(HWND h, UINT f, DWORD* c) { return m_real->RegisterStereoStatusWindow(h,f,c); }
HRESULT ProxyFactory::RegisterStereoStatusEvent(HANDLE h, DWORD* c) { return m_real->RegisterStereoStatusEvent(h,c); }

// FIX: void return, no return statement
void ProxyFactory::UnregisterStereoStatus(DWORD c) { m_real->UnregisterStereoStatus(c); }

HRESULT ProxyFactory::RegisterOcclusionStatusWindow(HWND h, UINT f, DWORD* c) { return m_real->RegisterOcclusionStatusWindow(h,f,c); }
HRESULT ProxyFactory::RegisterOcclusionStatusEvent(HANDLE h, DWORD* c) { return m_real->RegisterOcclusionStatusEvent(h,c); }

// FIX: void return
void ProxyFactory::UnregisterOcclusionStatus(DWORD c) { m_real->UnregisterOcclusionStatus(c); }

HRESULT ProxyFactory::CreateSwapChainForComposition(IUnknown* dev, const DXGI_SWAP_CHAIN_DESC1* desc,
    IDXGIOutput* out, IDXGISwapChain1** sc) {
    IDXGISwapChain1* realSC = nullptr;
    HRESULT hr = m_real->CreateSwapChainForComposition(dev, desc, out, &realSC);
    if (SUCCEEDED(hr)) return WrapSwapChain(dev, realSC, sc);
    return hr;
}

UINT ProxyFactory::GetCreationFlags() { return m_real->GetCreationFlags(); }
HRESULT ProxyFactory::EnumAdapterByLuid(LUID l, REFIID r, void** p) { return m_real->EnumAdapterByLuid(l,r,p); }
HRESULT ProxyFactory::EnumWarpAdapter(REFIID r, void** p) { return m_real->EnumWarpAdapter(r,p); }
HRESULT ProxyFactory::CheckFeatureSupport(DXGI_FEATURE f, void* s, UINT sz) { return m_real->CheckFeatureSupport(f,s,sz); }
HRESULT ProxyFactory::EnumAdapterByGpuPreference(UINT i, DXGI_GPU_PREFERENCE p, REFIID r, void** v) { return m_real->EnumAdapterByGpuPreference(i,p,r,v); }

HRESULT ProxyFactory::WrapSwapChain(IUnknown* pDevice, IDXGISwapChain1* realSC, IDXGISwapChain1** ppSC) {
    if (!realSC || !ppSC) return E_POINTER;
    AD_LOG_I("Wrapping SwapChain %p with device %p", realSC, pDevice);
    *ppSC = new ProxySwapChain(realSC, pDevice);
    return S_OK;
}

} // namespace adrena
