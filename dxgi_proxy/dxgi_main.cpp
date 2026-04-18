#include <adrena_core/config.h>
#include <adrena_core/logger.h>
#include <adrena_core/gpu_detect.h>
#include <adrena_core/shared_state.h>

#include <windows.h>
#include <dxgi1_4.h>

#include "proxy_factory.h"

static HMODULE g_realDXGI = nullptr;

static bool LoadRealDXGI() {
    if (g_realDXGI) return true;
    // Try renamed original first
    g_realDXGI = LoadLibraryA("dxgi.dll.orig");
    if (g_realDXGI) { AD_LOG_I("Loaded real dxgi from dxgi.dll.orig"); return true; }
    // System directory
    char sysDir[MAX_PATH]{};
    GetSystemDirectoryA(sysDir, MAX_PATH);
    strcat(sysDir, "\\dxgi.dll");
    g_realDXGI = LoadLibraryA(sysDir);
    if (g_realDXGI) { AD_LOG_I("Loaded real dxgi from system: %s", sysDir); return true; }
    AD_LOG_E("Failed to load real dxgi.dll");
    return false;
}

template<typename F>
F GetRealProc(const char* name) {
    if (!g_realDXGI) return nullptr;
    return reinterpret_cast<F>(GetProcAddress(g_realDXGI, name));
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID lpReserved) {
    switch (reason) {
    case DLL_PROCESS_ATTACH:
        DisableThreadLibraryCalls(hModule);
        adrena::Logger::Instance().Init("adrena_proxy.log");
        AD_LOG_I("adrenaproxy_dxgi.dll loaded (AdrenaProxy v2.0 — DXGI Proxy)");
        LoadRealDXGI();
        {
            adrena::Config& cfg = adrena::GetConfig();
            AD_LOG_I("Config: SGSR=%d Quality=%d Scale=%.2f FG=%d",
                     cfg.enabled, (int)cfg.quality, cfg.render_scale, (int)cfg.fg_mode);
            adrena::GPUInfo gpu = adrena::AutoDetectGPU();
            adrena::SharedState* ss = adrena::GetSharedState();
            if (ss) {
                adrena::SharedStateLock l(&ss->lock);
                ss->is_adreno = gpu.isAdreno;
                ss->adreno_tier = gpu.adrenoTier;
                ss->display_width = cfg.display_width;
                ss->display_height = cfg.display_height;
            }
        }
        break;
    case DLL_PROCESS_DETACH:
        AD_LOG_I("adrenaproxy_dxgi.dll unloading");
        adrena::Logger::Instance().Shutdown();
        if (g_realDXGI) { FreeLibrary(g_realDXGI); g_realDXGI = nullptr; }
        break;
    }
    return TRUE;
}

typedef HRESULT(WINAPI* PFN_CreateDXGIFactory)(REFIID, void**);
typedef HRESULT(WINAPI* PFN_CreateDXGIFactory1)(REFIID, void**);
typedef HRESULT(WINAPI* PFN_CreateDXGIFactory2)(UINT, REFIID, void**);

static HRESULT WrapFactory(REFIID riid, void** ppv) {
    if (!ppv) return E_POINTER;
    IDXGIFactory6* f6 = nullptr;
    if (SUCCEEDED(((IUnknown*)*ppv)->QueryInterface(__uuidof(IDXGIFactory6), (void**)&f6))) {
        ((IUnknown*)*ppv)->Release();
        auto* proxy = new adrena::ProxyFactory(f6);
        *ppv = proxy;
        return S_OK;
    }
    return S_OK;
}

extern "C" {

// No __declspec(dllexport) — the .def file handles exports.
// Using dllexport would conflict with dxgi.h declarations on MSVC.

HRESULT WINAPI CreateDXGIFactory(REFIID riid, void** ppFactory) {
    AD_LOG_I("CreateDXGIFactory called");
    auto fn = GetRealProc<PFN_CreateDXGIFactory>("CreateDXGIFactory");
    if (!fn) return E_FAIL;
    HRESULT hr = fn(riid, ppFactory);
    if (SUCCEEDED(hr)) hr = WrapFactory(riid, ppFactory);
    return hr;
}

HRESULT WINAPI CreateDXGIFactory1(REFIID riid, void** ppFactory) {
    AD_LOG_I("CreateDXGIFactory1 called");
    auto fn = GetRealProc<PFN_CreateDXGIFactory1>("CreateDXGIFactory1");
    if (!fn) return E_FAIL;
    HRESULT hr = fn(riid, ppFactory);
    if (SUCCEEDED(hr)) hr = WrapFactory(riid, ppFactory);
    return hr;
}

HRESULT WINAPI CreateDXGIFactory2(UINT Flags, REFIID riid, void** ppFactory) {
    AD_LOG_I("CreateDXGIFactory2 called: Flags=0x%X", Flags);
    auto fn = GetRealProc<PFN_CreateDXGIFactory2>("CreateDXGIFactory2");
    if (!fn) return E_FAIL;
    HRESULT hr = fn(Flags, riid, ppFactory);
    if (SUCCEEDED(hr)) hr = WrapFactory(riid, ppFactory);
    return hr;
}

HRESULT WINAPI DXGID3D10CreateDevice() {
    typedef HRESULT(WINAPI* PFN)();
    auto fn = GetRealProc<PFN>("DXGID3D10CreateDevice");
    return fn ? fn() : E_NOTIMPL;
}

HRESULT WINAPI DXGID3D10RegisterLayers() {
    typedef HRESULT(WINAPI* PFN)();
    auto fn = GetRealProc<PFN>("DXGID3D10RegisterLayers");
    return fn ? fn() : E_NOTIMPL;
}

} // extern "C"