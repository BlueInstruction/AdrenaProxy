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

extern "C" {

__declspec(dllexport) BOOL WINAPI GetFileVersionInfoA(LPCSTR lptstrFilename, DWORD dwHandle, DWORD dwLen, LPVOID lpData) {
    auto fn = GetReal<decltype(&GetFileVersionInfoA)>("GetFileVersionInfoA");
    return fn ? fn(lptstrFilename, dwHandle, dwLen, lpData) : FALSE;
}

__declspec(dllexport) BOOL WINAPI GetFileVersionInfoByHandle(DWORD dwHandle, DWORD dwLen, LPVOID lpData) {
    auto fn = GetReal<decltype(&GetFileVersionInfoByHandle)>("GetFileVersionInfoByHandle");
    return fn ? fn(dwHandle, dwLen, lpData) : FALSE;
}

__declspec(dllexport) BOOL WINAPI GetFileVersionInfoExA(DWORD dwFlags, LPCSTR lptstrFilename, DWORD dwHandle, DWORD dwLen, LPVOID lpData) {
    auto fn = GetReal<decltype(&GetFileVersionInfoExA)>("GetFileVersionInfoExA");
    return fn ? fn(dwFlags, lptstrFilename, dwHandle, dwLen, lpData) : FALSE;
}

__declspec(dllexport) BOOL WINAPI GetFileVersionInfoExW(DWORD dwFlags, LPCWSTR lptstrFilename, DWORD dwHandle, DWORD dwLen, LPVOID lpData) {
    auto fn = GetReal<decltype(&GetFileVersionInfoExW)>("GetFileVersionInfoExW");
    return fn ? fn(dwFlags, lptstrFilename, dwHandle, dwLen, lpData) : FALSE;
}

__declspec(dllexport) DWORD WINAPI GetFileVersionInfoSizeA(LPCSTR lptstrFilename, LPDWORD lpdwHandle) {
    auto fn = GetReal<decltype(&GetFileVersionInfoSizeA)>("GetFileVersionInfoSizeA");
    return fn ? fn(lptstrFilename, lpdwHandle) : 0;
}

__declspec(dllexport) DWORD WINAPI GetFileVersionInfoSizeExA(DWORD dwFlags, LPCSTR lptstrFilename, LPDWORD lpdwHandle) {
    auto fn = GetReal<decltype(&GetFileVersionInfoSizeExA)>("GetFileVersionInfoSizeExA");
    return fn ? fn(dwFlags, lptstrFilename, lpdwHandle) : 0;
}

__declspec(dllexport) DWORD WINAPI GetFileVersionInfoSizeExW(DWORD dwFlags, LPCWSTR lptstrFilename, LPDWORD lpdwHandle) {
    auto fn = GetReal<decltype(&GetFileVersionInfoSizeExW)>("GetFileVersionInfoSizeExW");
    return fn ? fn(dwFlags, lptstrFilename, lpdwHandle) : 0;
}

__declspec(dllexport) DWORD WINAPI GetFileVersionInfoSizeW(LPCWSTR lptstrFilename, LPDWORD lpdwHandle) {
    auto fn = GetReal<decltype(&GetFileVersionInfoSizeW)>("GetFileVersionInfoSizeW");
    return fn ? fn(lptstrFilename, lpdwHandle) : 0;
}

__declspec(dllexport) BOOL WINAPI GetFileVersionInfoW(LPCWSTR lptstrFilename, DWORD dwHandle, DWORD dwLen, LPVOID lpData) {
    auto fn = GetReal<decltype(&GetFileVersionInfoW)>("GetFileVersionInfoW");
    return fn ? fn(lptstrFilename, dwHandle, dwLen, lpData) : FALSE;
}

__declspec(dllexport) DWORD WINAPI VerFindFileA(DWORD dwFlags, LPCSTR lptstrFilename, LPCSTR lptstrWinDir, LPCSTR lptstrAppDir, LPSTR lptstrCurDir, PUINT lpuCurDirLen, LPSTR lptstrInstDir, PUINT lpuInstDirLen) {
    auto fn = GetReal<decltype(&VerFindFileA)>("VerFindFileA");
    return fn ? fn(dwFlags, lptstrFilename, lptstrWinDir, lptstrAppDir, lptstrCurDir, lpuCurDirLen, lptstrInstDir, lpuInstDirLen) : 0;
}

__declspec(dllexport) DWORD WINAPI VerFindFileW(DWORD dwFlags, LPCWSTR lptstrFilename, LPCWSTR lptstrWinDir, LPCWSTR lptstrAppDir, LPWSTR lptstrCurDir, PUINT lpuCurDirLen, LPWSTR lptstrInstDir, PUINT lpuInstDirLen) {
    auto fn = GetReal<decltype(&VerFindFileW)>("VerFindFileW");
    return fn ? fn(dwFlags, lptstrFilename, lptstrWinDir, lptstrAppDir, lptstrCurDir, lpuCurDirLen, lptstrInstDir, lpuInstDirLen) : 0;
}

__declspec(dllexport) DWORD WINAPI VerInstallFileA(DWORD dwFlags, LPCSTR lptstrSrcFilename, LPCSTR lptstrDestFilename, LPCSTR lptstrSrcDir, LPCSTR lptstrDestDir, LPCSTR lptstrCurDir, LPSTR lptstrTmpFile, PUINT lpuTmpFileLen) {
    auto fn = GetReal<decltype(&VerInstallFileA)>("VerInstallFileA");
    return fn ? fn(dwFlags, lptstrSrcFilename, lptstrDestFilename, lptstrSrcDir, lptstrDestDir, lptstrCurDir, lptstrTmpFile, lpuTmpFileLen) : 0;
}

__declspec(dllexport) DWORD WINAPI VerInstallFileW(DWORD dwFlags, LPCWSTR lptstrSrcFilename, LPCWSTR lptstrDestFilename, LPCWSTR lptstrSrcDir, LPCWSTR lptstrDestDir, LPCWSTR lptstrCurDir, LPWSTR lptstrTmpFile, PUINT lpuTmpFileLen) {
    auto fn = GetReal<decltype(&VerInstallFileW)>("VerInstallFileW");
    return fn ? fn(dwFlags, lptstrSrcFilename, lptstrDestFilename, lptstrSrcDir, lptstrDestDir, lptstrCurDir, lptstrTmpFile, lpuTmpFileLen) : 0;
}

__declspec(dllexport) DWORD WINAPI VerLanguageNameA(DWORD wLang, LPSTR szLang, UINT nSize) {
    auto fn = GetReal<decltype(&VerLanguageNameA)>("VerLanguageNameA");
    return fn ? fn(wLang, szLang, nSize) : 0;
}

__declspec(dllexport) DWORD WINAPI VerLanguageNameW(DWORD wLang, LPWSTR szLang, UINT nSize) {
    auto fn = GetReal<decltype(&VerLanguageNameW)>("VerLanguageNameW");
    return fn ? fn(wLang, szLang, nSize) : 0;
}

__declspec(dllexport) BOOL WINAPI VerQueryValueA(LPCVOID pBlock, LPCSTR lpSubBlock, LPVOID* lplpBuffer, PUINT puLen) {
    auto fn = GetReal<decltype(&VerQueryValueA)>("VerQueryValueA");
    return fn ? fn(pBlock, lpSubBlock, lplpBuffer, puLen) : FALSE;
}

__declspec(dllexport) BOOL WINAPI VerQueryValueW(LPCVOID pBlock, LPCWSTR lpSubBlock, LPVOID* lplpBuffer, PUINT puLen) {
    auto fn = GetReal<decltype(&VerQueryValueW)>("VerQueryValueW");
    return fn ? fn(pBlock, lpSubBlock, lplpBuffer, puLen) : FALSE;
}

} // extern "C"