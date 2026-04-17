#include "overlay_menu.h"
#include "config.h"
#include "gpu_detect.h"
#include "logger.h"

// FIX: Include ImGui headers OUTSIDE ifdef so declarations are always visible
// This prevents "identifier not found" errors
#ifdef ADRENA_OVERLAY_ENABLED
#include "imgui.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_dx11.h"
#include <d3d11.h>
#include <dxgi1_6.h>

#ifdef ADRENA_DX12_OVERLAY
#include "imgui_impl_dx12.h"
#include <d3d12.h>
#endif

// Forward declaration — required for ImGui v1.91.6-docking (C++ linkage, not extern "C")
LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
#endif

namespace adrena {

OverlayMenu& GetOverlayMenu() { static OverlayMenu menu; return menu; }
OverlayMenu::OverlayMenu() {}
OverlayMenu::~OverlayMenu() { Shutdown(); }

void OverlayMenu::Init(HWND hwnd) {
#ifdef ADRENA_OVERLAY_ENABLED
    if (m_initialized) return;
    IMGUI_CHECKVERSION(); ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    ImGui::StyleColorsDark();
    ImGuiStyle& style = ImGui::GetStyle(); style.WindowRounding = 6.0f; style.FrameRounding = 4.0f; style.Alpha = GetConfig().overlay_opacity;
    ImGui_ImplWin32_Init(hwnd);
    m_initialized = true;
    AD_LOG_I("Overlay Win32 initialized");
#endif
}

void OverlayMenu::Shutdown() {
#ifdef ADRENA_OVERLAY_ENABLED
    if (!m_initialized) return;
    if (m_d3d11Initialized) { ImGui_ImplDX11_Shutdown(); m_d3d11Initialized = false; }
#ifdef ADRENA_DX12_OVERLAY
    if (m_d3d12Initialized) { ImGui_ImplDX12_Shutdown(); m_d3d12Initialized = false; }
    if (m_srvHeap) { m_srvHeap->Release(); m_srvHeap = nullptr; }
#endif
    ImGui_ImplWin32_Shutdown(); ImGui::DestroyContext(); m_initialized = false;
#endif
}

void OverlayMenu::ToggleVisibility() {
    m_visible = !m_visible;
#ifdef ADRENA_OVERLAY_ENABLED
    ImGui::GetIO().WantCaptureMouse = m_visible;
    ImGui::GetIO().WantCaptureKeyboard = m_visible;
#endif
}

void OverlayMenu::HandleInput(UINT msg, WPARAM wParam, LPARAM lParam) {
#ifdef ADRENA_OVERLAY_ENABLED
    // FIX: Pass actual HWND instead of 0
    ImGui_ImplWin32_WndProcHandler(nullptr, msg, wParam, lParam);
#endif
}

void OverlayMenu::UpdateFPS() {
    m_frameCount++;
    LARGE_INTEGER freq, now; QueryPerformanceFrequency(&freq); QueryPerformanceCounter(&now);
    double t = (double)now.QuadPart / (double)freq.QuadPart;
    if (m_startTime == 0.0) m_startTime = t;
    if (t - m_lastFpsTime >= 1.0) {
        m_fps = (float)(m_frameCount - m_lastFpsFrames) / (float)(t - m_lastFpsTime);
        m_frameTime = (m_fps > 0.0f) ? (1000.0f / m_fps) : 0.0f;
        m_lastFpsTime = t; m_lastFpsFrames = m_frameCount;
    }
}

void OverlayMenu::DrawHUD() {
#ifdef ADRENA_OVERLAY_ENABLED
    auto& cfg = GetConfig();
    if (!cfg.fps_display) return;

    ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.5f);
    ImGui::Begin("##HUD", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoFocusOnAppearing);

    // FPS + Frame Time
    ImVec4 fpsColor = (m_fps >= 55.0f) ? ImVec4(0.2f, 1.0f, 0.2f, 1.0f) :
                      (m_fps >= 30.0f) ? ImVec4(1.0f, 1.0f, 0.2f, 1.0f) :
                                         ImVec4(1.0f, 0.3f, 0.3f, 1.0f);
    ImGui::TextColored(fpsColor, "FPS: %.0f", m_fps);
    ImGui::SameLine();
    ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "(%.1f ms)", m_frameTime);

    // GPU info
    static GPUInfo gpuInfo = DetectGPU();
    ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "GPU: %s", gpuInfo.name.c_str());
    if (gpuInfo.vramMB > 0)
        ImGui::Text("VRAM: %llu MB", gpuInfo.vramMB);

    // RAM usage
    MEMORYSTATUSEX memInfo = {}; memInfo.dwLength = sizeof(memInfo);
    if (GlobalMemoryStatusEx(&memInfo)) {
        DWORDLONG usedMB = (memInfo.ullTotalPhys - memInfo.ullAvailPhys) / (1024 * 1024);
        DWORDLONG totalMB = memInfo.ullTotalPhys / (1024 * 1024);
        ImGui::Text("RAM: %llu / %llu MB", usedMB, totalMB);
    }

    // SGSR status
    if (cfg.enabled && cfg.sgsr_mode != SGSRMode::Off) {
        ImGui::TextColored(ImVec4(0.2f, 1.0f, 0.5f, 1.0f), "SGSR: ON (%.0f%% scale)", cfg.render_scale * 100.0f);
    } else {
        ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "SGSR: OFF");
    }

    ImGui::End();
#endif
}

void OverlayMenu::DrawStartupNotification() {
#ifdef ADRENA_OVERLAY_ENABLED
    LARGE_INTEGER freq, now; QueryPerformanceFrequency(&freq); QueryPerformanceCounter(&now);
    double t = (double)now.QuadPart / (double)freq.QuadPart;
    double elapsed = t - m_startTime;

    // Show for 5 seconds after startup
    if (elapsed > 5.0 || m_startupNotifShown) {
        m_startupNotifShown = true;
        return;
    }

    // Fade out in last 1.5 seconds
    float alpha = (elapsed > 3.5) ? (float)(1.0 - (elapsed - 3.5) / 1.5) : 1.0f;

    ImGuiIO& io = ImGui::GetIO();
    float windowW = 320.0f;
    float windowH = 50.0f;
    ImGui::SetNextWindowPos(ImVec2(10, io.DisplaySize.y - windowH - 10), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(windowW, windowH));
    ImGui::SetNextWindowBgAlpha(0.7f * alpha);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 8.0f);
    ImGui::Begin("##Startup", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav);

    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.4f, 0.8f, 1.0f, alpha));
    ImGui::Text("AdrenaProxy v" ADRENA_PROXY_VERSION " Active");
    ImGui::PopStyleColor();
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.7f, 0.7f, 0.7f, alpha));
    ImGui::Text("Press HOME to open menu");
    ImGui::PopStyleColor();

    ImGui::End();
    ImGui::PopStyleVar();
#endif
}

void OverlayMenu::RenderFrame11(ID3D11Device* dev, ID3D11DeviceContext* ctx, IDXGISwapChain1* sc) {
#ifdef ADRENA_OVERLAY_ENABLED
    if(!dev || !ctx) return;
    if(!m_d3d11Initialized) {
        // Initialize ImGui context and Win32 backend if not yet done
        if (!m_initialized) {
            HWND hwnd = nullptr;
            sc->GetHwnd(&hwnd);
            Init(hwnd);
        }
        ImGui_ImplDX11_Init(dev, ctx); m_d3d11Initialized = true; m_initialized = true;
    }
    UpdateFPS(); ImGui_ImplDX11_NewFrame(); ImGui_ImplWin32_NewFrame(); ImGui::NewFrame();
    DrawHUD();
    DrawStartupNotification();
    if(m_visible) BuildUI();
    ImGui::Render(); ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
#endif
}

#ifdef ADRENA_DX12_OVERLAY
void OverlayMenu::InitDX12(ID3D12Device* dev, UINT bufferCount, DXGI_FORMAT format) {
    if (m_d3d12Initialized) return;
    if (!m_initialized) return; // Win32 backend must be initialized first via Init(hwnd)

    // Create SRV descriptor heap for ImGui font texture
    D3D12_DESCRIPTOR_HEAP_DESC srvDesc = {};
    srvDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    srvDesc.NumDescriptors = 1;
    srvDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    dev->CreateDescriptorHeap(&srvDesc, IID_PPV_ARGS(&m_srvHeap));

    ImGui_ImplDX12_Init(dev, bufferCount, format,
        m_srvHeap,
        m_srvHeap->GetCPUDescriptorHandleForHeapStart(),
        m_srvHeap->GetGPUDescriptorHandleForHeapStart());

    m_d3d12Initialized = true;
    AD_LOG_I("Overlay DX12 backend initialized");
}

void OverlayMenu::BeginDX12Frame() {
    if (!m_d3d12Initialized) return;
    UpdateFPS();
    ImGui_ImplDX12_NewFrame(); ImGui_ImplWin32_NewFrame(); ImGui::NewFrame();
    DrawHUD();
    DrawStartupNotification();
    if (m_visible) BuildUI();
    ImGui::Render();
}

void OverlayMenu::EndDX12Frame(ID3D12GraphicsCommandList* cmdList) {
    if (!m_d3d12Initialized || !cmdList) return;
    ID3D12DescriptorHeap* heaps[] = { m_srvHeap };
    cmdList->SetDescriptorHeaps(1, heaps);
    ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), cmdList);
}
#endif

void OverlayMenu::BuildUI() {
#ifdef ADRENA_OVERLAY_ENABLED
    auto& cfg = GetConfig();
    ImGui::SetNextWindowSize(ImVec2(460, 520), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("AdrenaProxy", &m_visible, ImGuiWindowFlags_NoCollapse)) { ImGui::End(); return; }
    ImGui::TextColored(ImVec4(0.4f, 0.7f, 1.0f, 1.0f), "AdrenaProxy - SGSR & Frame Generation");
    ImGui::Separator();
    if (ImGui::BeginTabBar("MainTabs")) {
        if (ImGui::BeginTabItem("SGSR")) { BuildSGSRTab(); ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("Frame Gen")) { BuildFGTab(); ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("Display")) { BuildDisplayTab(); ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("Advanced")) { BuildAdvancedTab(); ImGui::EndTabItem(); }
        ImGui::EndTabBar();
    }
    ImGui::End();
#endif
}

void OverlayMenu::BuildSGSRTab() {
#ifdef ADRENA_OVERLAY_ENABLED
    auto& cfg = GetConfig();
    if (ImGui::Checkbox("Enable SGSR", &cfg.enabled)) cfg.ApplyRenderScale();
    if (!cfg.enabled) return;
    const char* modes[] = { "Off", "SGSR 1 (Spatial)", "SGSR 2 (Temporal)" }; int modeIdx = (int)cfg.sgsr_mode;
    if (ImGui::Combo("Mode", &modeIdx, modes, 3)) cfg.sgsr_mode = (SGSRMode)modeIdx;
    const char* qualities[] = { "Ultra Quality (77%)", "Quality (67%)", "Balanced (59%)", "Performance (50%)", "Ultra Perf (33%)" }; int qIdx = (int)cfg.quality;
    if (ImGui::Combo("Quality", &qIdx, qualities, 5)) { cfg.quality = (Quality)qIdx; cfg.ApplyRenderScale(); }
    float scale = cfg.GetRenderScale(); if (ImGui::SliderFloat("Render Scale", &scale, 0.25f, 1.0f, "%.0f%%")) { cfg.custom_scale = scale; cfg.ApplyRenderScale(); }
    if (ImGui::SliderFloat("Sharpness", &cfg.sharpness, 0.0f, 2.0f, "%.2f")) {}
#endif
}

void OverlayMenu::BuildFGTab() {
#ifdef ADRENA_OVERLAY_ENABLED
    auto& cfg = GetConfig();
    const char* fgModes[] = { "x1 (Off)", "x2 (2x FPS)", "x3 (3x FPS)", "x4 (4x FPS)" }; int fgIdx = (int)cfg.fg_mode;
    if (ImGui::Combo("Frame Generation", &fgIdx, fgModes, 4)) cfg.fg_mode = (FGMode)fgIdx;
    if (cfg.fg_mode == FGMode::X1) return;
    const char* mq[] = { "Low", "Medium", "High" }; int mqIdx = (int)cfg.motion_quality;
    if (ImGui::Combo("Motion Quality", &mqIdx, mq, 3)) cfg.motion_quality = (MotionQuality)mqIdx;
    if (ImGui::SliderInt("Auto-disable FPS", &cfg.fps_threshold, 0, 120, cfg.fps_threshold == 0 ? "Off" : "%d FPS")) {}
#endif
}

void OverlayMenu::BuildDisplayTab() {
#ifdef ADRENA_OVERLAY_ENABLED
    auto& cfg = GetConfig();
    if (ImGui::Checkbox("Show HUD (FPS/GPU/RAM)", &cfg.fps_display)) {}
    if (ImGui::SliderFloat("Overlay Opacity", &cfg.overlay_opacity, 0.3f, 1.0f, "%.2f")) ImGui::GetStyle().Alpha = cfg.overlay_opacity;
    ImGui::Separator();
    ImGui::TextColored(ImVec4(0.4f, 0.7f, 1.0f, 1.0f), "Current Stats:");
    ImGui::Text("FPS: %.0f | Frame Time: %.1f ms", m_fps, m_frameTime);
    static GPUInfo gpuInfo = DetectGPU();
    ImGui::Text("GPU: %s", gpuInfo.name.c_str());
    ImGui::Text("VRAM: %llu MB", gpuInfo.vramMB);
    MEMORYSTATUSEX memInfo = {}; memInfo.dwLength = sizeof(memInfo);
    if (GlobalMemoryStatusEx(&memInfo)) {
        DWORDLONG usedMB = (memInfo.ullTotalPhys - memInfo.ullAvailPhys) / (1024 * 1024);
        DWORDLONG totalMB = memInfo.ullTotalPhys / (1024 * 1024);
        ImGui::Text("RAM: %llu / %llu MB (%lu%%)", usedMB, totalMB, memInfo.dwMemoryLoad);
    }
#endif
}

void OverlayMenu::BuildAdvancedTab() {
#ifdef ADRENA_OVERLAY_ENABLED
    auto& cfg = GetConfig(); static GPUInfo gpuInfo = DetectGPU();
    ImGui::TextColored(ImVec4(0.4f, 0.7f, 1.0f, 1.0f), "GPU: %s", gpuInfo.name.c_str());
    ImGui::Text("Tier: %s | Adreno: %s", GPUTierStr(gpuInfo.tier), gpuInfo.isAdreno ? "Yes" : "No");
    if (ImGui::Button("Save Config")) {
        char path[MAX_PATH] = {};
        HMODULE hMod = NULL;
        GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS, (LPCSTR)&GetConfig, &hMod);
        GetModuleFileNameA(hMod, path, MAX_PATH);
        char* sl = strrchr(path, '\\');
        if (sl) { strcpy(sl + 1, "adrena_proxy.ini"); cfg.Save(path); }
    }
#endif
}

} // namespace adrena
