#include "proxy_factory.h"
#include "config.h"
#include "logger.h"
#include "overlay_menu.h"
#include "gpu_detect.h"

#include <windows.h>
#include <dxgi1_6.h>

// ─── Real DXGI DLL ──────────────────────────────────
static HMODULE g_realDXGI = nullptr;
static char g_dllPath[MAX_PATH] = {};

static bool LoadRealDXGI()
{
    if (g_realDXGI) return true;

    // Try to load the real dxgi.dll from system directory
    char sysDir[MAX_PATH] = {};
    GetSystemDirectoryA(sysDir, MAX_PATH);
    strcat(sysDir, "\\dxgi.dll");

    g_realDXGI = LoadLibraryA(sysDir);
    if (!g_realDXGI) {
        // Fallback: try wine's dxgi.dll or original
        g_realDXGI = LoadLibraryA("dxgi.dll.orig");
    }

    return g_realDXGI != nullptr;
}

// ─── Proxy Functions ─────────────────────────────────
typedef HRESULT(WINAPI* PFN_CreateDXGIFactory)(REFIID, void**);
typedef HRESULT(WINAPI* PFN_CreateDXGIFactory1)(REFIID, void**);
typedef HRESULT(WINAPI* PFN_CreateDXGIFactory2)(UINT, REFIID, void**);

static HRESULT WrapFactoryResult(REFIID riid, void** ppv)
{
    if (!ppv) return E_POINTER;

    IDXGIFactory6* factory6 = nullptr;
    if (SUCCEEDED(((IUnknown*)*ppv)->QueryInterface(__uuidof(IDXGIFactory6), (void**)&factory6))) {
        auto* proxy = new adrena::ProxyFactory(factory6);
        *ppv = proxy;
        return S_OK;
    }

    return S_OK;
}

// ─── Exported Functions ──────────────────────────────

extern "C" __declspec(dllexport)
HRESULT WINAPI CreateDXGIFactory(REFIID riid, void** ppFactory)
{
    if (!LoadRealDXGI()) return E_FAIL;
    auto fn = (PFN_CreateDXGIFactory)GetProcAddress(g_realDXGI, "CreateDXGIFactory");
    if (!fn) return E_FAIL;

    HRESULT hr = fn(riid, ppFactory);
    if (SUCCEEDED(hr)) WrapFactoryResult(riid, ppFactory);
    return hr;
}

extern "C" __declspec(dllexport)
HRESULT WINAPI CreateDXGIFactory1(REFIID riid, void** ppFactory)
{
    if (!LoadRealDXGI()) return E_FAIL;
    auto fn = (PFN_CreateDXGIFactory1)GetProcAddress(g_realDXGI, "CreateDXGIFactory1");
    if (!fn) return E_FAIL;

    HRESULT hr = fn(riid, ppFactory);
    if (SUCCEEDED(hr)) WrapFactoryResult(riid, ppFactory);
    return hr;
}

extern "C" __declspec(dllexport)
HRESULT WINAPI CreateDXGIFactory2(UINT Flags, REFIID riid, void** ppFactory)
{
    if (!LoadRealDXGI()) return E_FAIL;
    auto fn = (PFN_CreateDXGIFactory2)GetProcAddress(g_realDXGI, "CreateDXGIFactory2");
    if (!fn) return CreateDXGIFactory1(riid, ppFactory);

    HRESULT hr = fn(Flags, riid, ppFactory);
    if (SUCCEEDED(hr)) WrapFactoryResult(riid, ppFactory);
    return hr;
}

extern "C" __declspec(dllexport)
HRESULT WINAPI DXGID3D10CreateDevice(HMODULE hModule, IDXGIFactory* pFactory,
    IDXGIAdapter* pAdapter, UINT Flags, void* pUnknown, void** ppDevice)
{
    if (!LoadRealDXGI()) return E_FAIL;
    auto fn = (decltype(&DXGID3D10CreateDevice))GetProcAddress(g_realDXGI, "DXGID3D10CreateDevice");
    return fn ? fn(hModule, pFactory, pAdapter, Flags, pUnknown, ppDevice) : E_NOTIMPL;
}

extern "C" __declspec(dllexport)
HRESULT WINAPI DXGID3D10CreateDeviceAndSwapChain(HMODULE hModule, IDXGIFactory* pFactory,
    IDXGIAdapter* pAdapter, UINT Flags, void* pUnknown, void* pSwapChainDesc,
    void** ppSwapChain, void** ppDevice)
{
    if (!LoadRealDXGI()) return E_FAIL;
    auto fn = (decltype(&DXGID3D10CreateDeviceAndSwapChain))GetProcAddress(g_realDXGI, "DXGID3D10CreateDeviceAndSwapChain");
    return fn ? fn(hModule, pFactory, pAdapter, Flags, pUnknown, pSwapChainDesc, ppSwapChain, ppDevice) : E_NOTIMPL;
}

extern "C" __declspec(dllexport)
HRESULT WINAPI DXGID3D10RegisterLayers(void* pLayers, UINT uiLayers)
{
    if (!LoadRealDXGI()) return E_FAIL;
    auto fn = (decltype(&DXGID3D10RegisterLayers))GetProcAddress(g_realDXGI, "DXGID3D10RegisterLayers");
    return fn ? fn(pLayers, uiLayers) : E_NOTIMPL;
}

// ─── DllMain — Entry Point ──────────────────────────
BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved)
{
    switch (ul_reason_for_call)
    {
    case DLL_PROCESS_ATTACH:
    {
        DisableThreadLibraryCalls(hModule);
        GetModuleFileNameA(hModule, g_dllPath, MAX_PATH);

        // Load Config
        char iniPath[MAX_PATH] = {};
        strcpy(iniPath, g_dllPath);
        char* sl = strrchr(iniPath, '\\');
        if (sl) strcpy(sl + 1, "adrena_proxy.ini");
        else strcpy(iniPath, "adrena_proxy.ini");

        auto& cfg = adrena::GetConfig();
        cfg.Load(iniPath);

        // Init Logger
        adrena::Logger::Init(adrena::LogLevel::Info);
        AD_LOG_I("=== AdrenaProxy v" ADRENA_PROXY_VERSION " Attached ===");
        AD_LOG_I("DLL Path: %s", g_dllPath);

        // Auto GPU Detect
        if (cfg.auto_detect_gpu) {
            auto gpu = adrena::DetectGPU();
            // Apply GPU tier recommendations to config
            if (gpu.isAdreno && cfg.fg_mode == adrena::FGMode::X1) {
                // Auto-suggest FG based on Adreno tier
                if (gpu.recommendedFG > 1) {
                    AD_LOG_I("Auto-enabling FG x%d for %s", gpu.recommendedFG, gpu.name.c_str());
                    // cfg.fg_mode = (adrena::FGMode)(gpu.recommendedFG - 1); // Optional
                }
            }
        }

        // Init Overlay
        // We defer HWND capture to the first SwapChain creation
        adrena::GetOverlayMenu();

        break;
    }
    case DLL_PROCESS_DETACH:
    {
        AD_LOG_I("=== AdrenaProxy Detaching ===");
        adrena::Logger::Shutdown();
        if (g_realDXGI) {
            FreeLibrary(g_realDXGI);
            g_realDXGI = nullptr;
        }
        break;
    }
    }
    return TRUE;
}
