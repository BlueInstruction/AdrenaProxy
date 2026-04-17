#include "proxy_factory.h"
#include "config.h"
#include "logger.h"
#include "overlay_menu.h"
#include "gpu_detect.h"

#include <windows.h>

// Do NOT include dxgi headers here — proxy_factory.h already includes them
// This avoids redefinition errors with CreateDXGIFactory etc.

static HMODULE g_realDXGI = nullptr;
static char g_dllPath[MAX_PATH] = {};

static bool LoadRealDXGI()
{
    if (g_realDXGI) return true;
    char sysDir[MAX_PATH] = {};
    GetSystemDirectoryA(sysDir, MAX_PATH);
    strcat(sysDir, "\\dxgi.dll");
    g_realDXGI = LoadLibraryA(sysDir);
    if (!g_realDXGI) g_realDXGI = LoadLibraryA("dxgi.dll.orig");
    return g_realDXGI != nullptr;
}

typedef HRESULT(WINAPI* PFN_CreateDXGIFactory)(REFIID, void**);
typedef HRESULT(WINAPI* PFN_CreateDXGIFactory1)(REFIID, void**);
typedef HRESULT(WINAPI* PFN_CreateDXGIFactory2)(UINT, REFIID, void**);

static HRESULT WrapFactoryResult(REFIID riid, void** ppv)
{
    if (!ppv) return E_POINTER;
    IDXGIFactory6* factory6 = nullptr;
    if (SUCCEEDED(((IUnknown*)*ppv)->QueryInterface(__uuidof(IDXGIFactory6), (void**)&factory6))) {
        ((IUnknown*)*ppv)->Release(); // Release original reference before overwriting
        auto* proxy = new adrena::ProxyFactory(factory6);
        *ppv = proxy;
        return S_OK;
    }
    return S_OK;
}

// FIX: No __declspec(dllexport) — the .def file handles exports
// This avoids C2375 redefinition with dxgi.h declarations
// All exported functions must use extern "C" so MinGW's linker can match
// the undecorated symbol names listed in dxgi.def.

extern "C" HRESULT WINAPI CreateDXGIFactory(REFIID riid, void** ppFactory)
{
    if (!LoadRealDXGI()) return E_FAIL;
    auto fn = (PFN_CreateDXGIFactory)GetProcAddress(g_realDXGI, "CreateDXGIFactory");
    if (!fn) return E_FAIL;
    HRESULT hr = fn(riid, ppFactory);
    if (SUCCEEDED(hr)) WrapFactoryResult(riid, ppFactory);
    return hr;
}

extern "C" HRESULT WINAPI CreateDXGIFactory1(REFIID riid, void** ppFactory)
{
    if (!LoadRealDXGI()) return E_FAIL;
    auto fn = (PFN_CreateDXGIFactory1)GetProcAddress(g_realDXGI, "CreateDXGIFactory1");
    if (!fn) return E_FAIL;
    HRESULT hr = fn(riid, ppFactory);
    if (SUCCEEDED(hr)) WrapFactoryResult(riid, ppFactory);
    return hr;
}

extern "C" HRESULT WINAPI CreateDXGIFactory2(UINT Flags, REFIID riid, void** ppFactory)
{
    if (!LoadRealDXGI()) return E_FAIL;
    auto fn = (PFN_CreateDXGIFactory2)GetProcAddress(g_realDXGI, "CreateDXGIFactory2");
    if (!fn) return CreateDXGIFactory1(riid, ppFactory);
    HRESULT hr = fn(Flags, riid, ppFactory);
    if (SUCCEEDED(hr)) WrapFactoryResult(riid, ppFactory);
    return hr;
}

// Use explicit function pointer types to avoid reliance on MinGW header declarations
typedef HRESULT(WINAPI* PFN_DXGID3D10CreateDevice)(HMODULE, IDXGIFactory*, IDXGIAdapter*, UINT, void*, void**);
typedef HRESULT(WINAPI* PFN_DXGID3D10CreateDeviceAndSwapChain)(HMODULE, IDXGIFactory*, IDXGIAdapter*, UINT, void*, void*, void**, void**);
typedef void(WINAPI* PFN_DXGID3D10RegisterLayers)(void*, UINT);
typedef HRESULT(WINAPI* PFN_DXGIGetDebugInterface1)(UINT, REFIID, void**);
typedef HRESULT(WINAPI* PFN_DXGIReportAdapterConfiguration)(UINT);

extern "C" HRESULT WINAPI DXGID3D10CreateDevice(HMODULE hModule, IDXGIFactory* pFactory,
    IDXGIAdapter* pAdapter, UINT Flags, void* pUnknown, void** ppDevice)
{
    if (!LoadRealDXGI()) return E_FAIL;
    auto fn = (PFN_DXGID3D10CreateDevice)GetProcAddress(g_realDXGI, "DXGID3D10CreateDevice");
    return fn ? fn(hModule, pFactory, pAdapter, Flags, pUnknown, ppDevice) : E_NOTIMPL;
}

extern "C" HRESULT WINAPI DXGID3D10CreateDeviceAndSwapChain(HMODULE hModule, IDXGIFactory* pFactory,
    IDXGIAdapter* pAdapter, UINT Flags, void* pUnknown, void* pSwapChainDesc,
    void** ppSwapChain, void** ppDevice)
{
    if (!LoadRealDXGI()) return E_FAIL;
    auto fn = (PFN_DXGID3D10CreateDeviceAndSwapChain)GetProcAddress(g_realDXGI, "DXGID3D10CreateDeviceAndSwapChain");
    return fn ? fn(hModule, pFactory, pAdapter, Flags, pUnknown, pSwapChainDesc, ppSwapChain, ppDevice) : E_NOTIMPL;
}

extern "C" void WINAPI DXGID3D10RegisterLayers(void* pLayers, UINT uiLayers)
{
    if (!LoadRealDXGI()) return;
    auto fn = (PFN_DXGID3D10RegisterLayers)GetProcAddress(g_realDXGI, "DXGID3D10RegisterLayers");
    if (fn) fn(pLayers, uiLayers);
}

extern "C" HRESULT WINAPI DXGIGetDebugInterface1(UINT Flags, REFIID riid, void** ppDebug)
{
    if (!LoadRealDXGI()) return E_FAIL;
    auto fn = (PFN_DXGIGetDebugInterface1)GetProcAddress(g_realDXGI, "DXGIGetDebugInterface1");
    return fn ? fn(Flags, riid, ppDebug) : E_NOTIMPL;
}

extern "C" HRESULT WINAPI DXGIReportAdapterConfiguration(UINT AdapterIndex)
{
    if (!LoadRealDXGI()) return E_FAIL;
    auto fn = (PFN_DXGIReportAdapterConfiguration)GetProcAddress(g_realDXGI, "DXGIReportAdapterConfiguration");
    return fn ? fn(AdapterIndex) : E_NOTIMPL;
}

// ─── DllMain ────────────────────────────────────────
extern "C" BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved)
{
    switch (ul_reason_for_call)
    {
    case DLL_PROCESS_ATTACH:
    {
        DisableThreadLibraryCalls(hModule);
        GetModuleFileNameA(hModule, g_dllPath, MAX_PATH);

        char iniPath[MAX_PATH] = {};
        strcpy(iniPath, g_dllPath);
        char* sl = strrchr(iniPath, '\\');
        if (sl) strcpy(sl + 1, "adrena_proxy.ini");
        else strcpy(iniPath, "adrena_proxy.ini");

        auto& cfg = adrena::GetConfig();
        cfg.Load(iniPath);

        adrena::Logger::Init(adrena::LogLevel::Info);
        AD_LOG_I("=== AdrenaProxy v" ADRENA_PROXY_VERSION " Attached ===");
        AD_LOG_I("DLL Path: %s", g_dllPath);

        if (cfg.auto_detect_gpu) {
            auto gpu = adrena::DetectGPU();
            AD_LOG_I("GPU: %s (Tier=%s)", gpu.name.c_str(), adrena::GPUTierStr(gpu.tier));
        }

        adrena::GetOverlayMenu();
        break;
    }
    case DLL_PROCESS_DETACH:
    {
        AD_LOG_I("=== AdrenaProxy Detaching ===");
        adrena::Logger::Shutdown();
        if (g_realDXGI) { FreeLibrary(g_realDXGI); g_realDXGI = nullptr; }
        break;
    }
    }
    return TRUE;
}
