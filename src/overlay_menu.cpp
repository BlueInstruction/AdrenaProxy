#include "overlay_menu.h"
#include "config.h"
#include "gpu_detect.h"
#include "logger.h"

#ifdef ADRENA_OVERLAY_ENABLED
#include "imgui.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_dx11.h"
#include "imgui_impl_dx12.h"
#include <d3d11.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#endif

namespace adrena {

OverlayMenu& GetOverlayMenu() {
    static OverlayMenu menu;
    return menu;
}

OverlayMenu::OverlayMenu() {}
OverlayMenu::~OverlayMenu() { Shutdown(); }

void OverlayMenu::Init(HWND hwnd)
{
#ifdef ADRENA_OVERLAY_ENABLED
    if (m_initialized) return;

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    // Style — dark theme like OptiScaler
    ImGui::StyleColorsDark();
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = 6.0f;
    style.FrameRounding = 4.0f;
    style.GrabRounding = 4.0f;
    style.WindowPadding = ImVec2(12, 12);
    style.FramePadding = ImVec2(8, 4);
    style.ItemSpacing = ImVec2(8, 6);
    style.Alpha = GetConfig().overlay_opacity;

    // Colors
    ImVec4* colors = style.Colors;
    colors[ImGuiCol_WindowBg] = ImVec4(0.08f, 0.08f, 0.12f, 0.94f);
    colors[ImGuiCol_TitleBg] = ImVec4(0.10f, 0.10f, 0.16f, 1.00f);
    colors[ImGuiCol_TitleBgActive] = ImVec4(0.16f, 0.16f, 0.28f, 1.00f);
    colors[ImGuiCol_Button] = ImVec4(0.20f, 0.20f, 0.35f, 1.00f);
    colors[ImGuiCol_ButtonHovered] = ImVec4(0.30f, 0.30f, 0.50f, 1.00f);
    colors[ImGuiCol_ButtonActive] = ImVec4(0.40f, 0.40f, 0.60f, 1.00f);
    colors[ImGuiCol_Header] = ImVec4(0.20f, 0.20f, 0.35f, 1.00f);
    colors[ImGuiCol_HeaderHovered] = ImVec4(0.30f, 0.30f, 0.50f, 1.00f);
    colors[ImGuiCol_Tab] = ImVec4(0.15f, 0.15f, 0.25f, 1.00f);
    colors[ImGuiCol_TabActive] = ImVec4(0.30f, 0.30f, 0.55f, 1.00f);
    colors[ImGuiCol_TabHovered] = ImVec4(0.35f, 0.35f, 0.60f, 1.00f);
    colors[ImGuiCol_CheckMark] = ImVec4(0.40f, 0.70f, 1.00f, 1.00f);
    colors[ImGuiCol_SliderGrab] = ImVec4(0.40f, 0.60f, 1.00f, 1.00f);
    colors[ImGuiCol_SliderGrabActive] = ImVec4(0.50f, 0.70f, 1.00f, 1.00f);

    ImGui_ImplWin32_Init(hwnd);
    AD_LOG_I("Overlay initialized (Win32)");
#endif
}

void OverlayMenu::Shutdown()
{
#ifdef ADRENA_OVERLAY_ENABLED
    if (!m_initialized) return;
    ImGui_ImplDX11_Shutdown();
    ImGui_ImplDX12_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
    m_initialized = false;
#endif
}

void OverlayMenu::ToggleVisibility()
{
    m_visible = !m_visible;
    auto& io = ImGui::GetIO();
    io.WantCaptureMouse = m_visible;
    io.WantCaptureKeyboard = m_visible;
}

void OverlayMenu::HandleInput(UINT msg, WPARAM wParam, LPARAM lParam)
{
#ifdef ADRENA_OVERLAY_ENABLED
    if (!ImGui::GetCurrentContext()) return;
    ImGui_ImplWin32_WndProcHandler((HWND)0, msg, wParam, lParam);
#endif
}

void OverlayMenu::UpdateFPS()
{
    m_frameCount++;
    LARGE_INTEGER freq, now;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&now);
    double t = (double)now.QuadPart / (double)freq.QuadPart;

    if (t - m_lastFpsTime >= 1.0) {
        m_fps = (float)(m_frameCount - m_lastFpsFrames) / (float)(t - m_lastFpsTime);
        m_lastFpsTime = t;
        m_lastFpsFrames = m_frameCount;
    }
}

// ─── D3D11 Render ────────────────────────────────────
void OverlayMenu::RenderFrame11(ID3D11Device* dev, ID3D11DeviceContext* ctx, IDXGISwapChain1* sc)
{
#ifdef ADRENA_OVERLAY_ENABLED
    if (!dev || !ctx) return;

    // Initialize DX11 backend once
    if (!m_d3d11Initialized) {
        ImGui_ImplDX11_Init(dev, ctx);
        m_d3d11Initialized = true;
        m_initialized = true;
        AD_LOG_I("Overlay DX11 backend initialized");
    }

    UpdateFPS();

    ImGui_ImplDX11_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();

    // FPS counter (always visible)
    if (GetConfig().fps_display) {
        ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_Always);
        ImGui::Begin("##FPS", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs |
                     ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoFocusOnAppearing |
                     ImGuiWindowFlags_NoNav);
        ImGui::Text("FPS: %.0f", m_fps);
        ImGui::End();
    }

    // Full menu
    if (m_visible) {
        BuildUI();
    }

    ImGui::Render();
    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
#endif
}

// ─── D3D12 Render ────────────────────────────────────
void OverlayMenu::RenderFrame12(ID3D12Device* dev, ID3D12CommandQueue* queue, IDXGISwapChain1* sc)
{
#ifdef ADRENA_OVERLAY_ENABLED
    if (!dev || !queue) return;

    // Initialize DX12 backend once
    if (!m_d3d12Initialized) {
        // Create descriptor heaps for ImGui DX12
        D3D12_DESCRIPTOR_HEAP_DESC rtvDesc = {};
        rtvDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
        rtvDesc.NumDescriptors = 4; // backbuffer count
        rtvDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
        dev->CreateDescriptorHeap(&rtvDesc, __uuidof(ID3D12DescriptorHeap), (void**)&m_rtvHeap12);

        D3D12_DESCRIPTOR_HEAP_DESC srvDesc = {};
        srvDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        srvDesc.NumDescriptors = 1;
        srvDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        dev->CreateDescriptorHeap(&srvDesc, __uuidof(ID3D12DescriptorHeap), (void**)&m_srvHeap12);

        // Get backbuffer format
        DXGI_SWAP_CHAIN_DESC1 scDesc = {};
        sc->GetDesc1(&scDesc);

        ImGui_ImplDX12_Init(dev, scDesc.BufferCount, scDesc.Format,
            m_srvHeap12->GetCPUDescriptorHandleForHeapStart(),
            m_srvHeap12->GetGPUDescriptorHandleForHeapStart());

        // Command allocator
        dev->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
            __uuidof(ID3D12CommandAllocator), (void**)&m_cmdAlloc12);

        // Command list
        dev->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT,
            m_cmdAlloc12, nullptr, __uuidof(ID3D12GraphicsCommandList), (void**)&m_cmdList12);
        m_cmdList12->Close();

        // Fence
        dev->CreateFence(0, D3D12_FENCE_FLAG_NONE, __uuidof(ID3D12Fence), (void**)&m_fence12);
        m_fenceEvent = CreateEventA(nullptr, FALSE, FALSE, nullptr);

        m_d3d12Initialized = true;
        m_initialized = true;
        AD_LOG_I("Overlay DX12 backend initialized");
    }

    UpdateFPS();

    UINT bbIndex = 0;
    sc->GetDesc1(nullptr); // get current index
    // Use GetCurrentBackBufferIndex if available
    IDXGISwapChain3* sc3 = nullptr;
    if (SUCCEEDED(sc->QueryInterface(__uuidof(IDXGISwapChain3), (void**)&sc3))) {
        bbIndex = sc3->GetCurrentBackBufferIndex();
        sc3->Release();
    }

    ImGui_ImplDX12_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();

    // FPS counter
    if (GetConfig().fps_display) {
        ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_Always);
        ImGui::Begin("##FPS", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs |
                     ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoFocusOnAppearing |
                     ImGuiWindowFlags_NoNav);
        ImGui::Text("FPS: %.0f", m_fps);
        ImGui::End();
    }

    if (m_visible) {
        BuildUI();
    }

    ImGui::Render();

    // Execute DX12 rendering
    m_cmdAlloc12->Reset();
    m_cmdList12->Reset(m_cmdAlloc12, nullptr);

    ID3D12Resource* backBuffer = nullptr;
    if (SUCCEEDED(sc->GetBuffer(bbIndex, __uuidof(ID3D12Resource), (void**)&backBuffer))) {
        D3D12_RESOURCE_BARRIER barrier = {};
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition.pResource = backBuffer;
        barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
        barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
        barrier.Transition.Subresource = 0;
        m_cmdList12->ResourceBarrier(1, &barrier);

        ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), m_cmdList12);

        barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
        barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
        m_cmdList12->ResourceBarrier(1, &barrier);

        backBuffer->Release();
    }

    m_cmdList12->Close();
    ID3D12CommandList* lists[] = { m_cmdList12 };
    queue->ExecuteCommandLists(1, lists);

    m_fenceValue++;
    queue->Signal(m_fence12, m_fenceValue);
    m_fence12->SetEventOnCompletion(m_fenceValue, (HANDLE)m_fenceEvent);
    WaitForSingleObject((HANDLE)m_fenceEvent, INFINITE);
#endif
}

// ─── Build UI ────────────────────────────────────────
void OverlayMenu::BuildUI()
{
#ifdef ADRENA_OVERLAY_ENABLED
    auto& cfg = GetConfig();

    ImGui::SetNextWindowSize(ImVec2(460, 520), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(ImVec2(100, 100), ImGuiCond_FirstUseEver);

    if (!ImGui::Begin("AdrenaProxy", &m_visible, ImGuiWindowFlags_NoCollapse)) {
        ImGui::End();
        return;
    }

    // Header
    ImGui::TextColored(ImVec4(0.4f, 0.7f, 1.0f, 1.0f), "AdrenaProxy — SGSR & Frame Generation");
    ImGui::Separator();

    if (ImGui::BeginTabBar("MainTabs")) {
        if (ImGui::BeginTabItem("SGSR")) {
            BuildSGSRTab();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Frame Gen")) {
            BuildFGTab();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Display")) {
            BuildDisplayTab();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Advanced")) {
            BuildAdvancedTab();
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }

    ImGui::End();
#endif
}

void OverlayMenu::BuildSGSRTab()
{
#ifdef ADRENA_OVERLAY_ENABLED
    auto& cfg = GetConfig();

    // Enable
    if (ImGui::Checkbox("Enable SGSR", &cfg.enabled)) {
        cfg.ApplyRenderScale();
    }

    if (!cfg.enabled) return;

    // Mode
    const char* modes[] = { "Off", "SGSR 1 (Spatial)", "SGSR 2 (Temporal)" };
    int modeIdx = (int)cfg.sgsr_mode;
    if (ImGui::Combo("Mode", &modeIdx, modes, 3)) {
        cfg.sgsr_mode = (SGSRMode)modeIdx;
    }

    ImGui::Spacing();

    // Quality
    const char* qualities[] = { "Ultra Quality (77%)", "Quality (67%)", "Balanced (59%)", "Performance (50%)", "Ultra Performance (33%)" };
    int qIdx = (int)cfg.quality;
    if (ImGui::Combo("Quality Preset", &qIdx, qualities, 5)) {
        cfg.quality = (Quality)qIdx;
        cfg.ApplyRenderScale();
    }

    // Custom scale
    ImGui::Spacing();
    float scale = cfg.GetRenderScale();
    if (ImGui::SliderFloat("Render Scale", &scale, 0.25f, 1.0f, "%.0f%%")) {
        cfg.custom_scale = scale;
        cfg.ApplyRenderScale();
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("Reset")) {
        cfg.custom_scale = 0.0f;
        cfg.ApplyRenderScale();
    }

    // Sharpness
    ImGui::Spacing();
    if (ImGui::SliderFloat("Sharpness", &cfg.sharpness, 0.0f, 2.0f, "%.2f")) {}

    // Info
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f),
        "Render: %dx%d → Display: (auto)",
        (int)(1920 * cfg.render_scale), (int)(1080 * cfg.render_scale));

    if (cfg.sgsr_mode == SGSRMode::SGSR2) {
        ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f),
            "⚠ SGSR2 is experimental — requires depth buffer access");
    }
#endif
}

void OverlayMenu::BuildFGTab()
{
#ifdef ADRENA_OVERLAY_ENABLED
    auto& cfg = GetConfig();

    // FG Mode
    const char* fgModes[] = { "x1 (Off)", "x2 (2x FPS)", "x3 (3x FPS)", "x4 (4x FPS)" };
    int fgIdx = (int)cfg.fg_mode;
    if (ImGui::Combo("Frame Generation", &fgIdx, fgModes, 4)) {
        cfg.fg_mode = (FGMode)fgIdx;
    }

    if (cfg.fg_mode == FGMode::X1) return;

    ImGui::Spacing();

    // Motion quality
    const char* mq[] = { "Low (Fast)", "Medium (Balanced)", "High (Quality)" };
    int mqIdx = (int)cfg.motion_quality;
    if (ImGui::Combo("Motion Quality", &mqIdx, mq, 3)) {
        cfg.motion_quality = (MotionQuality)mqIdx;
    }

    // FPS threshold
    ImGui::Spacing();
    if (ImGui::SliderInt("Auto-disable FPS", &cfg.fps_threshold, 0, 120, cfg.fps_threshold == 0 ? "Off" : "%d FPS")) {}

    // Warning
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "⚠ Compute-based interpolation");
    ImGui::TextColored(ImVec4(0.8f, 0.6f, 0.2f, 1.0f), "  Quality below DLSS 3 / FSR 3 FG");

    if (cfg.fg_mode >= FGMode::X3) {
        ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "⚠ x3/x4 increases latency significantly");
    }
    if (cfg.fg_mode == FGMode::X4) {
        ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "⚠ x4 recommended only for Adreno 8xx");
    }
#endif
}

void OverlayMenu::BuildDisplayTab()
{
#ifdef ADRENA_OVERLAY_ENABLED
    auto& cfg = GetConfig();

    if (ImGui::Checkbox("FPS Counter", &cfg.fps_display)) {}

    ImGui::Spacing();
    if (ImGui::SliderFloat("Overlay Opacity", &cfg.overlay_opacity, 0.3f, 1.0f, "%.2f")) {
        ImGui::GetStyle().Alpha = cfg.overlay_opacity;
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::TextColored(ImVec4(0.4f, 0.7f, 1.0f, 1.0f), "FPS: %.0f", m_fps);
#endif
}

void OverlayMenu::BuildAdvancedTab()
{
#ifdef ADRENA_OVERLAY_ENABLED
    auto& cfg = GetConfig();

    // GPU info
    static GPUInfo gpuInfo = DetectGPU();
    ImGui::TextColored(ImVec4(0.4f, 0.7f, 1.0f, 1.0f), "GPU: %s", gpuInfo.name.c_str());
    ImGui::Text("Tier: %s | VRAM: %llu MB", GPUTierStr(gpuInfo.tier), gpuInfo.vramMB);
    ImGui::Text("Adreno: %s | FP16: %s | WG: %d",
                gpuInfo.isAdreno ? "Yes" : "No",
                gpuInfo.useFP16 ? "Yes" : "No",
                gpuInfo.workgroupSize);

    ImGui::Spacing();
    ImGui::Separator();

    // VSync
    const char* vsyncModes[] = { "Game Default", "Force Off", "Force On" };
    if (ImGui::Combo("VSync", &cfg.vsync, vsyncModes, 3)) {}

    // Force D3D11
    if (ImGui::Checkbox("Force D3D11 Path", &cfg.force_d3d11)) {}

    ImGui::Spacing();

    // Save config
    if (ImGui::Button("Save Config")) {
        char path[MAX_PATH] = {};
        HMODULE hMod = nullptr;
        GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
                           (LPCSTR)&GetConfig, &hMod);
        GetModuleFileNameA(hMod, path, MAX_PATH);
        char* sl = strrchr(path, '\\');
        if (sl) { strcpy(sl + 1, "adrena_proxy.ini"); cfg.Save(path); }
    }
    ImGui::SameLine();

    // Reset
    if (ImGui::Button("Reset Defaults")) {
        cfg = Config();
        cfg.ApplyRenderScale();
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::TextColored(ImVec4(0.4f, 0.4f, 0.4f, 1.0f),
        "AdrenaProxy v" ADRENA_PROXY_VERSION);
#endif
}

} // namespace adrena