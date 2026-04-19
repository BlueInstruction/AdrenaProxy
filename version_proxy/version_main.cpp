#include <adrena_core/logger.h>
#include <windows.h>

// version.dll proxy — lightweight alternative entry point.
// Some games/mods already use dxgi.dll (e.g., ReShade, DXVK).
// This DLL loads our DXGI proxy from a different entry point.
//
// How it works:
// 1. Game loads version.dll (ours) instead of the system one
// 2. We load the real version.dll from system directory
// 3. We load adrenaproxy_dxgi.dll which hooks the swapchain
// 4. We forward all version.dll calls to the real DLL

static HMODULE g_realVersion = nullptr;
static HMODULE g_ourDXGI    = nullptr;

static void LoadRealVersion() {
    if (g_realVersion) return;
    char sysDir[MAX_PATH]{};
    GetSystemDirectoryA(sysDir, MAX_PATH);
    strcat(sysDir, "\\version.dll");
    g_realVersion = LoadLibraryA(sysDir);
    if (g_realVersion) {
        AD_LOG_I("version.dll proxy: loaded real from %s", sysDir);
    } else {
        AD_LOG_E("version.dll proxy: failed to load real version.dll");
    }
}

static void LoadOurDXGI() {
    if (g_ourDXGI) return;
    // Load our DXGI proxy — it will hook the swapchain
    g_ourDXGI = LoadLibraryA("adrenaproxy_dxgi.dll");
    if (!g_ourDXGI) {
        // Fallback: try dxgi.dll if already installed
        g_ourDXGI = LoadLibraryA("dxgi.dll");
    }
    if (g_ourDXGI) {
        AD_LOG_I("version.dll proxy: loaded adrenaproxy_dxgi");
    } else {
        AD_LOG_W("version.dll proxy: could not load adrenaproxy_dxgi");
    }
}

template<typename F>
static F GetReal(const char* name) {
    if (!g_realVersion) return nullptr;
    return reinterpret_cast<F>(GetProcAddress(g_realVersion, name));
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID lpReserved) {
    switch (reason) {
    case DLL_PROCESS_ATTACH:
        DisableThreadLibraryCalls(hModule);
        adrena::Logger::Instance().Init("adrena_proxy.log");
        AD_LOG_I("adrenaproxy_version.dll loaded (AdrenaProxy v2.0 — version.dll proxy)");
        LoadRealVersion();
        LoadOurDXGI();
        break;
    case DLL_PROCESS_DETACH:
        AD_LOG_I("adrenaproxy_version.dll unloading");
        adrena::Logger::Instance().Shutdown();
        // Don't free — may still be in use
        break;
    }
    return TRUE;
}

// ── Forward all version.dll exports ──
// No __declspec(dllexport) — the .def file handles exports.
// Use explicit function pointer types to avoid decltype conflicts with winver.h.

typedef BOOL   (WINAPI* PFN_GFVIA)(LPCSTR, DWORD, DWORD, LPVOID);
typedef BOOL   (WINAPI* PFN_GFVIBH)(DWORD, DWORD, LPVOID);

extern "C" {

BOOL WINAPI ap_GetFileVersionInfoA(LPCSTR lptstrFilename, DWORD dwHandle, DWORD dwLen, LPVOID lpData) {
    auto fn = GetReal<PFN_GFVIA>("GetFileVersionInfoA");
    return fn ? fn(lptstrFilename, dwHandle, dwLen, lpData) : FALSE;
}

BOOL WINAPI ap_GetFileVersionInfoByHandle(DWORD dwHandle, DWORD dwLen, LPVOID lpData) {
    auto fn = GetReal<PFN_GFVIBH>("GetFileVersionInfoByHandle");
    return fn ? fn(dwHandle, dwLen, lpData) : FALSE;
}

typedef BOOL   (WINAPI* PFN_GFVIEA)(DWORD, LPCSTR, DWORD, DWORD, LPVOID);
typedef BOOL   (WINAPI* PFN_GFVIEW)(DWORD, LPCWSTR, DWORD, DWORD, LPVOID);
typedef DWORD  (WINAPI* PFN_GFVISA)(LPCSTR, LPDWORD);
typedef DWORD  (WINAPI* PFN_GFVISEA)(DWORD, LPCSTR, LPDWORD);
typedef DWORD  (WINAPI* PFN_GFVISEW)(DWORD, LPCWSTR, LPDWORD);
typedef DWORD  (WINAPI* PFN_GFVISW)(LPCWSTR, LPDWORD);
typedef BOOL   (WINAPI* PFN_GFVIW)(LPCWSTR, DWORD, DWORD, LPVOID);
typedef DWORD  (WINAPI* PFN_VFFA)(DWORD, LPCSTR, LPCSTR, LPCSTR, LPSTR, PUINT, LPSTR, PUINT);
typedef DWORD  (WINAPI* PFN_VFFW)(DWORD, LPCWSTR, LPCWSTR, LPCWSTR, LPWSTR, PUINT, LPWSTR, PUINT);
typedef DWORD  (WINAPI* PFN_VIFA)(DWORD, LPCSTR, LPCSTR, LPCSTR, LPCSTR, LPCSTR, LPSTR, PUINT);
typedef DWORD  (WINAPI* PFN_VIFW)(DWORD, LPCWSTR, LPCWSTR, LPCWSTR, LPCWSTR, LPCWSTR, LPWSTR, PUINT);
typedef DWORD  (WINAPI* PFN_VLNA)(DWORD, LPSTR, UINT);
typedef DWORD  (WINAPI* PFN_VLNW)(DWORD, LPWSTR, UINT);
typedef BOOL   (WINAPI* PFN_VQVA)(LPCVOID, LPCSTR, LPVOID*, PUINT);
typedef BOOL   (WINAPI* PFN_VQVW)(LPCVOID, LPCWSTR, LPVOID*, PUINT);

BOOL WINAPI ap_GetFileVersionInfoExA(DWORD f, LPCSTR n, DWORD h, DWORD l, LPVOID d) {
    auto fn = GetReal<PFN_GFVIEA>("GetFileVersionInfoExA"); return fn ? fn(f,n,h,l,d) : FALSE; }
BOOL WINAPI ap_GetFileVersionInfoExW(DWORD f, LPCWSTR n, DWORD h, DWORD l, LPVOID d) {
    auto fn = GetReal<PFN_GFVIEW>("GetFileVersionInfoExW"); return fn ? fn(f,n,h,l,d) : FALSE; }
DWORD WINAPI ap_GetFileVersionInfoSizeA(LPCSTR n, LPDWORD h) {
    auto fn = GetReal<PFN_GFVISA>("GetFileVersionInfoSizeA"); return fn ? fn(n,h) : 0; }
DWORD WINAPI ap_GetFileVersionInfoSizeExA(DWORD f, LPCSTR n, LPDWORD h) {
    auto fn = GetReal<PFN_GFVISEA>("GetFileVersionInfoSizeExA"); return fn ? fn(f,n,h) : 0; }
DWORD WINAPI ap_GetFileVersionInfoSizeExW(DWORD f, LPCWSTR n, LPDWORD h) {
    auto fn = GetReal<PFN_GFVISEW>("GetFileVersionInfoSizeExW"); return fn ? fn(f,n,h) : 0; }
DWORD WINAPI ap_GetFileVersionInfoSizeW(LPCWSTR n, LPDWORD h) {
    auto fn = GetReal<PFN_GFVISW>("GetFileVersionInfoSizeW"); return fn ? fn(n,h) : 0; }
BOOL WINAPI ap_GetFileVersionInfoW(LPCWSTR n, DWORD h, DWORD l, LPVOID d) {
    auto fn = GetReal<PFN_GFVIW>("GetFileVersionInfoW"); return fn ? fn(n,h,l,d) : FALSE; }
DWORD WINAPI ap_VerFindFileA(DWORD f, LPCSTR n, LPCSTR w, LPCSTR a, LPSTR c, PUINT cl, LPSTR i, PUINT il) {
    auto fn = GetReal<PFN_VFFA>("VerFindFileA"); return fn ? fn(f,n,w,a,c,cl,i,il) : 0; }
DWORD WINAPI ap_VerFindFileW(DWORD f, LPCWSTR n, LPCWSTR w, LPCWSTR a, LPWSTR c, PUINT cl, LPWSTR i, PUINT il) {
    auto fn = GetReal<PFN_VFFW>("VerFindFileW"); return fn ? fn(f,n,w,a,c,cl,i,il) : 0; }
DWORD WINAPI ap_VerInstallFileA(DWORD f, LPCSTR s, LPCSTR d, LPCSTR sd, LPCSTR dd, LPCSTR cd, LPSTR t, PUINT tl) {
    auto fn = GetReal<PFN_VIFA>("VerInstallFileA"); return fn ? fn(f,s,d,sd,dd,cd,t,tl) : 0; }
DWORD WINAPI ap_VerInstallFileW(DWORD f, LPCWSTR s, LPCWSTR d, LPCWSTR sd, LPCWSTR dd, LPCWSTR cd, LPWSTR t, PUINT tl) {
    auto fn = GetReal<PFN_VIFW>("VerInstallFileW"); return fn ? fn(f,s,d,sd,dd,cd,t,tl) : 0; }
DWORD WINAPI ap_VerLanguageNameA(DWORD l, LPSTR s, UINT n) {
    auto fn = GetReal<PFN_VLNA>("VerLanguageNameA"); return fn ? fn(l,s,n) : 0; }
DWORD WINAPI ap_VerLanguageNameW(DWORD l, LPWSTR s, UINT n) {
    auto fn = GetReal<PFN_VLNW>("VerLanguageNameW"); return fn ? fn(l,s,n) : 0; }
BOOL WINAPI ap_VerQueryValueA(LPCVOID b, LPCSTR s, LPVOID* p, PUINT l) {
    auto fn = GetReal<PFN_VQVA>("VerQueryValueA"); return fn ? fn(b,s,p,l) : FALSE; }
BOOL WINAPI ap_VerQueryValueW(LPCVOID b, LPCWSTR s, LPVOID* p, PUINT l) {
    auto fn = GetReal<PFN_VQVW>("VerQueryValueW"); return fn ? fn(b,s,p,l) : FALSE; }

} // extern "C"