#include "adrena_core/overlay_menu.h"
#include "adrena_core/config.h"
#include "adrena_core/shared_state.h"
#include "adrena_core/logger.h"

#ifdef ADRENA_OVERLAY_ENABLED
#include <imgui.h>
#include <imgui_impl_win32.h>
#ifdef ADRENA_DX12_OVERLAY
#include <imgui_impl_dx12.h>
#endif
#include <imgui_impl_dx11.h>
// Forward declaration for ImGui Win32 WndProc handler
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
#endif

#include <d3d12.h>
#include <dxgi1_4.h>

namespace adrena {

// ────────────────────────────────────────────────────────
// Construction / Destruction
// ────────────────────────────────────────────────────────

OverlayMenu::~OverlayMenu() {
    Shutdown();
}

// ────────────────────────────────────────────────────────
// Win32 Init (ImGui + input)
// ────────────────────────────────────────────────────────

bool OverlayMenu::InitWin32(HWND hwnd) {
#ifdef ADRENA_OVERLAY_ENABLED
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.IniFilename = nullptr; // Don't save imgui.ini

    // ── Premium dark theme — Adreno blue accent ──
    ImGui::StyleColorsDark();
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding    = 8.0f;
    style.ChildRounding     = 6.0f;
    style.FrameRounding     = 5.0f;
    style.PopupRounding     = 6.0f;
    style.GrabRounding      = 4.0f;
    style.TabRounding       = 5.0f;
    style.ScrollbarRounding = 8.0f;
    style.WindowPadding     = ImVec2(14, 14);
    style.FramePadding      = ImVec2(10, 5);
    style.ItemSpacing       = ImVec2(10, 7);
    style.ItemInnerSpacing  = ImVec2(8, 4);
    style.WindowBorderSize  = 1.0f;
    style.FrameBorderSize   = 0.0f;
    style.TabBorderSize     = 0.0f;
    style.GrabMinSize       = 12.0f;
    style.ScrollbarSize     = 12.0f;
    style.WindowTitleAlign  = ImVec2(0.5f, 0.5f);

    ImVec4* c = style.Colors;
    // Background
    c[ImGuiCol_WindowBg]           = ImVec4(0.06f, 0.06f, 0.10f, 0.96f);
    c[ImGuiCol_ChildBg]            = ImVec4(0.07f, 0.07f, 0.11f, 0.60f);
    c[ImGuiCol_PopupBg]            = ImVec4(0.08f, 0.08f, 0.12f, 0.94f);
    // Title bar
    c[ImGuiCol_TitleBg]            = ImVec4(0.08f, 0.08f, 0.14f, 1.00f);
    c[ImGuiCol_TitleBgActive]      = ImVec4(0.12f, 0.14f, 0.24f, 1.00f);
    c[ImGuiCol_TitleBgCollapsed]   = ImVec4(0.06f, 0.06f, 0.10f, 0.75f);
    // Tabs — blue accent
    c[ImGuiCol_Tab]                = ImVec4(0.10f, 0.12f, 0.20f, 0.90f);
    c[ImGuiCol_TabHovered]         = ImVec4(0.22f, 0.35f, 0.65f, 0.85f);
    c[ImGuiCol_TabActive]          = ImVec4(0.18f, 0.28f, 0.55f, 1.00f);
    c[ImGuiCol_TabUnfocused]       = ImVec4(0.08f, 0.08f, 0.14f, 0.90f);
    c[ImGuiCol_TabUnfocusedActive] = ImVec4(0.12f, 0.18f, 0.35f, 1.00f);
    // Frame (sliders, inputs, combos)
    c[ImGuiCol_FrameBg]            = ImVec4(0.10f, 0.10f, 0.16f, 1.00f);
    c[ImGuiCol_FrameBgHovered]     = ImVec4(0.16f, 0.18f, 0.30f, 1.00f);
    c[ImGuiCol_FrameBgActive]      = ImVec4(0.20f, 0.24f, 0.40f, 1.00f);
    // Buttons
    c[ImGuiCol_Button]             = ImVec4(0.14f, 0.18f, 0.32f, 0.80f);
    c[ImGuiCol_ButtonHovered]      = ImVec4(0.22f, 0.30f, 0.55f, 0.90f);
    c[ImGuiCol_ButtonActive]       = ImVec4(0.28f, 0.38f, 0.65f, 1.00f);
    // Interactive accents — Adreno blue
    c[ImGuiCol_CheckMark]          = ImVec4(0.35f, 0.65f, 1.00f, 1.00f);
    c[ImGuiCol_SliderGrab]         = ImVec4(0.30f, 0.55f, 1.00f, 0.85f);
    c[ImGuiCol_SliderGrabActive]   = ImVec4(0.40f, 0.70f, 1.00f, 1.00f);
    // Headers (collapsing, tree nodes)
    c[ImGuiCol_Header]             = ImVec4(0.14f, 0.18f, 0.32f, 0.55f);
    c[ImGuiCol_HeaderHovered]      = ImVec4(0.22f, 0.30f, 0.52f, 0.70f);
    c[ImGuiCol_HeaderActive]       = ImVec4(0.26f, 0.36f, 0.60f, 0.85f);
    // Separator
    c[ImGuiCol_Separator]          = ImVec4(0.20f, 0.25f, 0.45f, 0.40f);
    c[ImGuiCol_SeparatorHovered]   = ImVec4(0.30f, 0.45f, 0.80f, 0.60f);
    c[ImGuiCol_SeparatorActive]    = ImVec4(0.35f, 0.55f, 1.00f, 0.80f);
    // Scrollbar
    c[ImGuiCol_ScrollbarBg]        = ImVec4(0.05f, 0.05f, 0.08f, 0.60f);
    c[ImGuiCol_ScrollbarGrab]      = ImVec4(0.20f, 0.25f, 0.40f, 0.80f);
    c[ImGuiCol_ScrollbarGrabHovered]= ImVec4(0.28f, 0.35f, 0.55f, 0.90f);
    c[ImGuiCol_ScrollbarGrabActive]= ImVec4(0.35f, 0.45f, 0.70f, 1.00f);
    // Text
    c[ImGuiCol_Text]               = ImVec4(0.90f, 0.92f, 0.96f, 1.00f);
    c[ImGuiCol_TextDisabled]       = ImVec4(0.45f, 0.47f, 0.52f, 1.00f);
    // Border
    c[ImGuiCol_Border]             = ImVec4(0.20f, 0.25f, 0.42f, 0.40f);
    c[ImGuiCol_BorderShadow]       = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
    // Resize grip
    c[ImGuiCol_ResizeGrip]         = ImVec4(0.22f, 0.35f, 0.65f, 0.25f);
    c[ImGuiCol_ResizeGripHovered]  = ImVec4(0.30f, 0.50f, 0.90f, 0.50f);
    c[ImGuiCol_ResizeGripActive]   = ImVec4(0.35f, 0.60f, 1.00f, 0.75f);

    ImGui_ImplWin32_Init(hwnd);
    AD_LOG_I("Overlay Win32 initialized");
    return true;
#else
    return false;
#endif
}

// ────────────────────────────────────────────────────────
// DX12 Init (rendering backend)
// ────────────────────────────────────────────────────────

bool OverlayMenu::InitDX12(ID3D12Device* device, DXGI_FORMAT rtvFormat) {
#ifdef ADRENA_DX12_OVERLAY
    if (m_initialized) return true;
    if (!device) return false;

    m_device = device;

    // RTV heap — one descriptor per backbuffer (we recreate per frame)
    D3D12_DESCRIPTOR_HEAP_DESC rtvDesc{};
    rtvDesc.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    rtvDesc.NumDescriptors = 1;
    rtvDesc.Flags          = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    HRESULT hr = device->CreateDescriptorHeap(&rtvDesc,
        IID_ID3D12DescriptorHeap, (void**)&m_rtvHeap);
    if (FAILED(hr)) {
        AD_LOG_E("Overlay: Failed to create RTV heap");
        return false;
    }

    // SRV heap for ImGui font atlas
    D3D12_DESCRIPTOR_HEAP_DESC srvDesc{};
    srvDesc.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    srvDesc.NumDescriptors = 1;
    srvDesc.Flags          = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    hr = device->CreateDescriptorHeap(&srvDesc,
        IID_ID3D12DescriptorHeap, (void**)&m_srvHeap);
    if (FAILED(hr)) {
        AD_LOG_E("Overlay: Failed to create SRV heap");
        return false;
    }

    // Command allocator
    hr = device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
        IID_ID3D12CommandAllocator, (void**)&m_cmdAlloc);
    if (FAILED(hr)) {
        AD_LOG_E("Overlay: Failed to create command allocator");
        return false;
    }

    // Command list
    hr = device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT,
        m_cmdAlloc, nullptr, IID_ID3D12GraphicsCommandList, (void**)&m_cmdList);
    if (FAILED(hr)) {
        AD_LOG_E("Overlay: Failed to create command list");
        return false;
    }
    m_cmdList->Close();

    // Fence for GPU synchronization (non-blocking)
    hr = device->CreateFence(0, D3D12_FENCE_FLAG_NONE,
        IID_ID3D12Fence, (void**)&m_fence);
    if (FAILED(hr)) {
        AD_LOG_E("Overlay: Failed to create fence");
        return false;
    }
    m_fenceEvent = CreateEventA(nullptr, FALSE, FALSE, nullptr);
    if (!m_fenceEvent) {
        AD_LOG_E("Overlay: Failed to create fence event");
        return false;
    }

    // Initialize ImGui DX12 backend
    ImGui_ImplDX12_Init(device, 1, rtvFormat, m_srvHeap,
        m_srvHeap->GetCPUDescriptorHandleForHeapStart(),
        m_srvHeap->GetGPUDescriptorHandleForHeapStart());

    m_initialized = true;
    AD_LOG_I("Overlay DX12 backend initialized");
    return true;
#else
    AD_LOG_W("DX12 overlay not available (build without MSVC)");
    return false;
#endif
}

// ────────────────────────────────────────────────────────
// Shutdown
// ────────────────────────────────────────────────────────

void OverlayMenu::Shutdown() {
#ifdef ADRENA_OVERLAY_ENABLED
    if (!m_initialized) return;

#ifdef ADRENA_DX12_OVERLAY
    ImGui_ImplDX12_Shutdown();
#endif
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();

    if (m_cmdList)    { m_cmdList->Release();    m_cmdList = nullptr; }
    if (m_cmdAlloc)   { m_cmdAlloc->Release();   m_cmdAlloc = nullptr; }
    if (m_fence)      { m_fence->Release();      m_fence = nullptr; }
    if (m_rtvHeap)    { m_rtvHeap->Release();    m_rtvHeap = nullptr; }
    if (m_srvHeap)    { m_srvHeap->Release();    m_srvHeap = nullptr; }
    if (m_fenceEvent) { CloseHandle(m_fenceEvent); m_fenceEvent = nullptr; }

    m_device      = nullptr;
    m_initialized = false;
    AD_LOG_I("Overlay shut down");
#endif
}

// ────────────────────────────────────────────────────────
// Render — called once per frame from ProxySwapChain::PresentImpl
// ────────────────────────────────────────────────────────

void OverlayMenu::Render(ID3D12CommandQueue* cmdQueue,
                         ID3D12Resource* backbuffer,
                         uint32_t width, uint32_t height) {
#ifdef ADRENA_DX12_OVERLAY
    if (!m_initialized || !backbuffer || !cmdQueue) return;

    // ── Wait for previous overlay frame to finish on GPU ──
    if (m_fence && m_fenceVal > 0 && m_fence->GetCompletedValue() < m_fenceVal) {
        m_fence->SetEventOnCompletion(m_fenceVal, m_fenceEvent);
        WaitForSingleObject(m_fenceEvent, INFINITE);
    }

    // ── Begin ImGui frame ──
    ImGui_ImplDX12_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();

    // Build UI if visible
    if (m_visible) BuildUI();

    // Always render HUD (FPS counter etc.)
    RenderHUD(width, height);

    ImGui::Render();

    // ── Use overlay's own command list ──
    m_cmdAlloc->Reset();
    m_cmdList->Reset(m_cmdAlloc, nullptr);

    // Transition backbuffer: PRESENT → RENDER_TARGET
    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource   = backbuffer;
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
    barrier.Transition.StateAfter  = D3D12_RESOURCE_STATE_RENDER_TARGET;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    m_cmdList->ResourceBarrier(1, &barrier);

    // Create RTV for this backbuffer
    D3D12_RENDER_TARGET_VIEW_DESC rtvDesc{};
    rtvDesc.Format         = DXGI_FORMAT_R8G8B8A8_UNORM;
    rtvDesc.ViewDimension  = D3D12_RTV_DIMENSION_TEXTURE2D;
    m_device->CreateRenderTargetView(backbuffer, &rtvDesc,
        m_rtvHeap->GetCPUDescriptorHandleForHeapStart());

    D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle =
        m_rtvHeap->GetCPUDescriptorHandleForHeapStart();
    m_cmdList->OMSetRenderTargets(1, &rtvHandle, FALSE, nullptr);

    ID3D12DescriptorHeap* heaps[] = { m_srvHeap };
    m_cmdList->SetDescriptorHeaps(1, heaps);

    ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), m_cmdList);

    // Transition backbuffer: RENDER_TARGET → PRESENT
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
    barrier.Transition.StateAfter  = D3D12_RESOURCE_STATE_PRESENT;
    m_cmdList->ResourceBarrier(1, &barrier);

    m_cmdList->Close();

    // ── Submit overlay command list to GPU ──
    ID3D12CommandList* lists[] = { m_cmdList };
    cmdQueue->ExecuteCommandLists(1, lists);

    // ── Signal fence so next frame waits for completion ──
    m_fenceVal++;
    cmdQueue->Signal(m_fence, m_fenceVal);

#endif
}

// ────────────────────────────────────────────────────────
// Get the overlay's command list for submission
// (used by ProxySwapChain to submit overlay work)
// ────────────────────────────────────────────────────────

// This is accessed via friend or public member — ProxySwapChain reads m_cmdList

// ────────────────────────────────────────────────────────
// Build UI — main menu window
// ────────────────────────────────────────────────────────

void OverlayMenu::BuildUI() {
#ifdef ADRENA_OVERLAY_ENABLED
    Config& cfg = GetConfig();
    SharedState* ss = GetSharedState();

    ImGui::SetNextWindowSize(ImVec2(440, 380), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowBgAlpha(0.96f);

    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.30f, 0.50f, 1.00f, 0.30f));

    if (!ImGui::Begin("AdrenaProxy v2.0##Main", &m_visible,
        ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoScrollbar)) {
        ImGui::End();
        ImGui::PopStyleColor();
        ImGui::PopStyleVar();
        return;
    }

    // ── Header bar ──
    ImGui::TextColored(ImVec4(0.40f, 0.70f, 1.00f, 1.0f), "AdrenaProxy");
    ImGui::SameLine();
    ImGui::TextColored(ImVec4(0.55f, 0.55f, 0.60f, 1.0f), "v2.0");

    if (ss) {
        SharedStateLock l(&ss->lock);
        if (ss->is_adreno) {
            ImGui::SameLine(ImGui::GetWindowWidth() - 110);
            ImGui::TextColored(ImVec4(0.20f, 1.00f, 0.40f, 1.0f), "Adreno Tier %d", ss->adreno_tier);
        }
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // ── Tab bar ──
    if (ImGui::BeginTabBar("##Tabs", ImGuiTabBarFlags_None)) {

        // ═══════════════════════════════════════════
        // Tab: SGSR
        // ═══════════════════════════════════════════
        if (ImGui::BeginTabItem("SGSR")) {

            ImGui::Checkbox("Enable SGSR", &cfg.enabled);

            // SGSR Mode selector
            const char* modes[] = { "Off", "SGSR1 (Spatial)", "SGSR2 (Temporal)" };
            int mode = (int)cfg.sgsr_mode;
            if (ImGui::Combo("Mode", &mode, modes, 3)) {
                cfg.sgsr_mode = (SGSRMode)mode;
                if (ss) {
                    SharedStateLock l(&ss->lock);
                    ss->sgsr_enabled = cfg.enabled;
                }
            }

            // SGSR2 experimental warning
            if (cfg.sgsr_mode == SGSRMode::SGSR2) {
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.8f, 0.0f, 1.0f));
                ImGui::TextWrapped("SGSR2 is experimental — requires depth + motion vectors.");
                ImGui::TextWrapped("Only works in DLSS-compatible games via adrenaproxy_sgsr.dll.");
                ImGui::PopStyleColor();
                ImGui::Spacing();
            }

            // Quality preset
            const char* qualities[] = {
                "Ultra Quality (77%)",
                "Quality (67%)",
                "Balanced (59%)",
                "Performance (50%)",
                "Ultra Performance (33%)"
            };
            int q = (int)cfg.quality;
            if (ImGui::Combo("Quality", &q, qualities, 5)) {
                cfg.quality = (Quality)q;
                cfg.ApplyRenderScale();
                if (ss) {
                    SharedStateLock l(&ss->lock);
                    ss->render_scale = cfg.render_scale;
                }
            }

            // Custom scale override
            if (ImGui::SliderFloat("Render Scale", &cfg.custom_scale, 0.0f, 1.0f, "%.2f")) {
                if (cfg.custom_scale < 0.05f) cfg.custom_scale = 0.0f; // 0 = auto
                cfg.ApplyRenderScale();
                if (ss) {
                    SharedStateLock l(&ss->lock);
                    ss->render_scale = cfg.render_scale;
                }
            }

            // Sharpness
            if (ImGui::SliderFloat("Sharpness", &cfg.sharpness, 0.0f, 2.0f, "%.2f")) {
                if (ss) {
                    SharedStateLock l(&ss->lock);
                    ss->sharpness = cfg.sharpness;
                }
            }

            // DLSS path indicator
            if (ss) {
                SharedStateLock l(&ss->lock);
                if (ss->sgsr_active) {
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.2f, 1.0f, 0.4f, 1.0f));
                    ImGui::Text("Path: DLSS Proxy (real upscaling via adrenaproxy_sgsr.dll)");
                    ImGui::Text("Render: %ux%u → Display: %ux%u",
                        ss->render_width, ss->render_height,
                        ss->display_width, ss->display_height);
                    ImGui::PopStyleColor();
                } else {
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.8f, 0.8f, 0.8f, 1.0f));
                    ImGui::Text("Path: DXGI Proxy (sharpening only — no DLSS detected)");
                    ImGui::PopStyleColor();
                }
            }

            ImGui::EndTabItem();
        }

        // ═══════════════════════════════════════════
        // Tab: Frame Generation
        // ═══════════════════════════════════════════
        if (ImGui::BeginTabItem("Frame Gen")) {

            const char* fgModes[] = { "Off (x1)", "x2", "x3", "x4" };
            int fg = (int)cfg.fg_mode;
            if (ImGui::Combo("Mode", &fg, fgModes, 4)) {
                cfg.fg_mode = (FGMode)fg;
                if (ss) {
                    SharedStateLock l(&ss->lock);
                    ss->fg_mode = (int32_t)cfg.fg_mode + 1;
                }
            }

            ImGui::Spacing();

            // FG explanation
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.7f, 0.8f, 0.9f, 1.0f));
            ImGui::TextWrapped("Pure Extra Present mode — adds empty Present() calls");
            ImGui::TextWrapped("to stimulate the Vulkan queue on Turnip/Adreno.");
            ImGui::TextWrapped("Zero GPU compute overhead. No backbuffer index change");
            ImGui::TextWrapped("(fixes overlay flickering).");
            ImGui::PopStyleColor();

            ImGui::Spacing();

            // FPS threshold
            if (ImGui::SliderInt("FPS Threshold", &cfg.fps_threshold, 0, 120)) {
                // 0 = always active, else auto-disable above threshold
            }
            if (cfg.fps_threshold > 0) {
                ImGui::SameLine();
                ImGui::TextDisabled("(auto-disable above %d FPS)", cfg.fps_threshold);
            } else {
                ImGui::SameLine();
                ImGui::TextDisabled("(always active)");
            }

            // Current FG status
            if (ss) {
                SharedStateLock l(&ss->lock);
                int curFg = ss->fg_mode;
                ImGui::Spacing();
                ImGui::Text("Active FG: x%d (%d extra presents/frame)",
                    curFg, curFg - 1);
            }

            ImGui::EndTabItem();
        }

        // ═══════════════════════════════════════════
        // Tab: Display
        // ═══════════════════════════════════════════
        if (ImGui::BeginTabItem("Display")) {

            ImGui::Checkbox("Show FPS Counter", &cfg.fps_display);
            if (ss) {
                SharedStateLock l(&ss->lock);
                ss->fps_display = cfg.fps_display;
            }

            ImGui::Checkbox("Show Overlay", &cfg.overlay_enabled);
            if (ss) {
                SharedStateLock l(&ss->lock);
                ss->overlay_visible = cfg.overlay_enabled;
            }

            ImGui::SliderFloat("Overlay Opacity", &cfg.overlay_opacity, 0.3f, 1.0f, "%.2f");

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            // Key bindings info
            ImGui::TextDisabled("Key Bindings:");
            ImGui::BulletText("HOME — Toggle this menu");
            ImGui::BulletText("Changes apply instantly (no restart needed)");

            ImGui::EndTabItem();
        }

        // ═══════════════════════════════════════════
        // Tab: Advanced
        // ═══════════════════════════════════════════
        if (ImGui::BeginTabItem("Advanced")) {

            // GPU info
            if (ss) {
                SharedStateLock l(&ss->lock);
                ImGui::Text("GPU: %s (Tier: Adreno %d)",
                    ss->is_adreno ? "Adreno" : "Other",
                    ss->adreno_tier);
                ImGui::Text("DLSS Proxy: %s", ss->sgsr_active ? "Active" : "Inactive");
            }

            ImGui::Spacing();

            // D3D options
            ImGui::Checkbox("Force D3D11", &cfg.force_d3d11);
            ImGui::SliderInt("Max Frame Queue", &cfg.max_frame_queue, 1, 5);

            // VSync
            if (ImGui::SliderInt("VSync", &cfg.vsync, 0, 2)) {
                // 0=off, 1=on, 2=adaptive
            }

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            // Reset defaults button
            if (ImGui::Button("Reset All to Defaults", ImVec2(-1, 0))) {
                cfg = Config();
                cfg.ApplyRenderScale();
                if (ss) {
                    SharedStateLock l(&ss->lock);
                    ss->fg_mode         = 1;
                    ss->overlay_visible = true;
                    ss->fps_display     = true;
                    ss->sharpness       = cfg.sharpness;
                    ss->render_scale    = cfg.render_scale;
                }
                AD_LOG_I("Config reset to defaults");
            }

            ImGui::Spacing();

            // Save config button
            if (ImGui::Button("Save Config to INI", ImVec2(-1, 0))) {
                cfg.Save(GetConfigPath());
                AD_LOG_I("Config saved to INI");
            }

            ImGui::EndTabItem();
        }

        ImGui::EndTabBar();
    }

    ImGui::End();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar();
#endif
}

// ────────────────────────────────────────────────────────
// Render HUD — FPS counter and status overlay
// ────────────────────────────────────────────────────────

void OverlayMenu::RenderHUD(int width, int height) {
#ifdef ADRENA_OVERLAY_ENABLED
    Config& cfg = GetConfig();

    // Don't render HUD if FPS display is off and menu is hidden
    if (!cfg.fps_display && !m_visible) return;

    // ── Smoothed FPS / Frame Time (updated every 0.5s) ──
    static float s_fps = 0.0f;
    static float s_frameTime = 0.0f;
    static float s_lastUpdate = 0.0f;
    float now = (float)ImGui::GetTime();
    if (now - s_lastUpdate > 0.5f) {
        s_fps = ImGui::GetIO().Framerate;
        s_frameTime = (s_fps > 0.0f) ? (1000.0f / s_fps) : 0.0f;
        s_lastUpdate = now;
    }

    // ── System stats (updated every 1s to avoid overhead) ──
    static DWORDLONG s_ramUsedMB = 0, s_ramTotalMB = 0;
    static float s_lastSysUpdate = 0.0f;
    if (now - s_lastSysUpdate > 1.0f) {
        MEMORYSTATUSEX memInfo = {}; memInfo.dwLength = sizeof(memInfo);
        if (GlobalMemoryStatusEx(&memInfo)) {
            s_ramUsedMB  = (memInfo.ullTotalPhys - memInfo.ullAvailPhys) / (1024 * 1024);
            s_ramTotalMB = memInfo.ullTotalPhys / (1024 * 1024);
        }
        s_lastSysUpdate = now;
    }

    // ── HUD Window (top-left corner) ──
    if (cfg.fps_display || m_visible) {
        ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_Always);
        ImGui::SetNextWindowBgAlpha(0.55f);
        ImGuiWindowFlags hudFlags =
            ImGuiWindowFlags_NoDecoration     |
            ImGuiWindowFlags_NoInputs         |
            ImGuiWindowFlags_AlwaysAutoResize |
            ImGuiWindowFlags_NoSavedSettings  |
            ImGuiWindowFlags_NoFocusOnAppearing|
            ImGuiWindowFlags_NoNav;

        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 8.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10, 8));
        ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.05f, 0.05f, 0.10f, 1.0f));

        if (ImGui::Begin("##HUD", nullptr, hudFlags)) {
            SharedState* ss = GetSharedState();

            // ── FPS + Frame Time (big, color-coded) ──
            if (cfg.fps_display) {
                ImVec4 fpsColor;
                if (s_fps >= 60)       fpsColor = ImVec4(0.20f, 1.00f, 0.40f, 1.0f);
                else if (s_fps >= 30)  fpsColor = ImVec4(1.00f, 0.85f, 0.20f, 1.0f);
                else                   fpsColor = ImVec4(1.00f, 0.30f, 0.30f, 1.0f);

                ImGui::TextColored(fpsColor, "%.0f FPS", s_fps);
                ImGui::SameLine();
                ImGui::TextColored(ImVec4(0.60f, 0.60f, 0.65f, 1.0f), "%.1f ms", s_frameTime);
            }

            // ── Feature status pills ──
            if (ss) {
                SharedStateLock l(&ss->lock);

                // SGSR status
                if (ss->sgsr_enabled || ss->sgsr_active) {
                    ImVec4 col = ss->sgsr_active
                        ? ImVec4(0.20f, 1.00f, 0.40f, 1.0f)
                        : ImVec4(0.90f, 0.80f, 0.20f, 1.0f);
                    ImGui::TextColored(col, "SGSR %s",
                        ss->sgsr_active ? "ON (DLSS)" : "ON (Sharp)");
                } else {
                    ImGui::TextColored(ImVec4(0.45f, 0.45f, 0.50f, 1.0f), "SGSR OFF");
                }

                // FG status on same line
                ImGui::SameLine();
                ImGui::TextColored(ImVec4(0.30f, 0.30f, 0.35f, 1.0f), "|");
                ImGui::SameLine();
                if (ss->fg_mode > 1) {
                    ImGui::TextColored(ImVec4(0.40f, 0.75f, 1.00f, 1.0f),
                        "FG x%d", ss->fg_mode);
                } else {
                    ImGui::TextColored(ImVec4(0.45f, 0.45f, 0.50f, 1.0f), "FG OFF");
                }

                // Resolution (if DLSS path)
                if (ss->sgsr_active && ss->render_width > 0) {
                    ImGui::TextColored(ImVec4(0.50f, 0.50f, 0.55f, 1.0f),
                        "%ux%u > %ux%u (%.0f%%)",
                        ss->render_width, ss->render_height,
                        ss->display_width, ss->display_height,
                        ss->render_scale * 100.0f);
                }
            }

            // ── System info line ──
            if (s_ramTotalMB > 0) {
                ImGui::TextColored(ImVec4(0.50f, 0.50f, 0.55f, 1.0f),
                    "RAM %llu/%llu MB", s_ramUsedMB, s_ramTotalMB);
            }

            // ── GPU name (compact) ──
            if (ss) {
                SharedStateLock l(&ss->lock);
                if (ss->is_adreno) {
                    ImGui::TextColored(ImVec4(0.40f, 0.70f, 1.00f, 1.0f),
                        "Adreno (Tier %d)", ss->adreno_tier);
                }
            }
        }
        ImGui::End();

        ImGui::PopStyleColor();
        ImGui::PopStyleVar(2);
    }

    // ── Startup notification (fades out after 4 seconds) ──
    static float showTime = (float)ImGui::GetTime();
    float elapsed = now - showTime;
    if (elapsed < 4.0f) {
        float alpha = (elapsed < 3.0f) ? 0.90f : 0.90f * (1.0f - (elapsed - 3.0f));

        ImGui::SetNextWindowPos(
            ImVec2(width * 0.5f - 180.0f, (float)height - 65.0f), ImGuiCond_Always);
        ImGui::SetNextWindowBgAlpha(alpha * 0.7f);

        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 10.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(16, 10));
        ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.06f, 0.06f, 0.12f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.30f, 0.50f, 1.00f, alpha * 0.4f));

        ImGuiWindowFlags notifyFlags =
            ImGuiWindowFlags_NoDecoration     |
            ImGuiWindowFlags_NoInputs         |
            ImGuiWindowFlags_AlwaysAutoResize |
            ImGuiWindowFlags_NoSavedSettings  |
            ImGuiWindowFlags_NoFocusOnAppearing|
            ImGuiWindowFlags_NoNav;

        if (ImGui::Begin("##Notify", nullptr, notifyFlags)) {
            ImGui::TextColored(ImVec4(0.40f, 0.70f, 1.00f, alpha),
                "AdrenaProxy v2.0");
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(0.65f, 0.65f, 0.70f, alpha),
                "— Press HOME for settings");
        }
        ImGui::End();

        ImGui::PopStyleColor(2);
        ImGui::PopStyleVar(2);
    }
#endif
}

// ────────────────────────────────────────────────────────
// WndProc — input handling for overlay toggle
// ────────────────────────────────────────────────────────

LRESULT CALLBACK OverlayMenu::WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
#ifdef ADRENA_OVERLAY_ENABLED
    // Forward input to ImGui
    if (ImGui_ImplWin32_WndProcHandler(hwnd, msg, wp, lp))
        return true;
#endif
    return 0;
}

} // namespace adrena