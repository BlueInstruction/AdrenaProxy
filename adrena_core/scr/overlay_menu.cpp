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

    // Dark theme with Adreno-inspired colors
    ImGui::StyleColorsDark();
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding    = 6.0f;
    style.FrameRounding     = 4.0f;
    style.GrabRounding      = 4.0f;
    style.TabRounding       = 4.0f;
    style.ScrollbarRounding = 6.0f;
    style.WindowPadding     = ImVec2(12, 12);
    style.FramePadding      = ImVec2(8, 4);
    style.ItemSpacing       = ImVec2(8, 6);

    ImVec4* colors = style.Colors;
    colors[ImGuiCol_WindowBg]           = ImVec4(0.08f, 0.08f, 0.12f, 0.94f);
    colors[ImGuiCol_TitleBg]            = ImVec4(0.10f, 0.10f, 0.16f, 1.00f);
    colors[ImGuiCol_TitleBgActive]      = ImVec4(0.16f, 0.16f, 0.28f, 1.00f);
    colors[ImGuiCol_Tab]                = ImVec4(0.12f, 0.12f, 0.20f, 0.86f);
    colors[ImGuiCol_TabHovered]         = ImVec4(0.28f, 0.28f, 0.50f, 0.80f);
    colors[ImGuiCol_TabActive]          = ImVec4(0.20f, 0.20f, 0.40f, 1.00f);
    colors[ImGuiCol_FrameBg]            = ImVec4(0.12f, 0.12f, 0.18f, 1.00f);
    colors[ImGuiCol_FrameBgHovered]     = ImVec4(0.20f, 0.20f, 0.32f, 1.00f);
    colors[ImGuiCol_FrameBgActive]      = ImVec4(0.24f, 0.24f, 0.40f, 1.00f);
    colors[ImGuiCol_Button]             = ImVec4(0.20f, 0.20f, 0.36f, 0.65f);
    colors[ImGuiCol_ButtonHovered]      = ImVec4(0.30f, 0.30f, 0.52f, 0.80f);
    colors[ImGuiCol_ButtonActive]       = ImVec4(0.36f, 0.36f, 0.60f, 1.00f);
    colors[ImGuiCol_CheckMark]          = ImVec4(0.40f, 0.70f, 1.00f, 1.00f);
    colors[ImGuiCol_SliderGrab]         = ImVec4(0.40f, 0.70f, 1.00f, 0.80f);
    colors[ImGuiCol_SliderGrabActive]   = ImVec4(0.50f, 0.80f, 1.00f, 1.00f);
    colors[ImGuiCol_Header]             = ImVec4(0.20f, 0.20f, 0.36f, 0.50f);
    colors[ImGuiCol_HeaderHovered]      = ImVec4(0.28f, 0.28f, 0.48f, 0.60f);
    colors[ImGuiCol_HeaderActive]       = ImVec4(0.32f, 0.32f, 0.54f, 0.80f);
    colors[ImGuiCol_Text]               = ImVec4(0.92f, 0.92f, 0.96f, 1.00f);
    colors[ImGuiCol_TextDisabled]       = ImVec4(0.50f, 0.50f, 0.56f, 1.00f);
    colors[ImGuiCol_Border]             = ImVec4(0.28f, 0.28f, 0.40f, 0.50f);

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

void OverlayMenu::Render(ID3D12GraphicsCommandList* externalCmdList,
                         ID3D12Resource* backbuffer,
                         uint32_t width, uint32_t height) {
#ifdef ADRENA_DX12_OVERLAY
    if (!m_initialized || !backbuffer) return;

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

    // ── Execute overlay command list (non-blocking) ──
    // The caller (ProxySwapChain) will execute this on the command queue
    // We store it so the swapchain can submit it
    // NOTE: The caller must call ExecuteCommandLists before the final Present

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

    ImGui::SetNextWindowSize(ImVec2(420, 340), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowBgAlpha(0.95f);

    if (!ImGui::Begin("AdrenaProxy v2.0", &m_visible,
        ImGuiWindowFlags_NoCollapse)) {
        ImGui::End();
        return;
    }

    // ── Title bar info ──
    ImGui::TextColored(ImVec4(0.4f, 0.7f, 1.0f, 1.0f), "AdrenaProxy");
    ImGui::SameLine();
    ImGui::TextDisabled("v2.0 — SGSR Upscaling + Frame Gen");

    if (ss) {
        SharedStateLock l(&ss->lock);
        if (ss->is_adreno) {
            ImGui::SameLine(ImGui::GetWindowWidth() - 100);
            ImGui::TextColored(ImVec4(0.2f, 1.0f, 0.4f, 1.0f), "Adreno %d", ss->adreno_tier);
        }
    }

    ImGui::Separator();

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
#endif
}

// ────────────────────────────────────────────────────────
// Render HUD — FPS counter and status overlay
// ────────────────────────────────────────────────────────

void OverlayMenu::RenderHUD(int width, int height) {
#ifdef ADRENA_OVERLAY_ENABLED
    Config& cfg = GetConfig();

    // Don't render HUD if both FPS and overlay are hidden
    if (!cfg.fps_display && !m_visible) return;

    // ── FPS Counter + Status (top-left) ──
    if (cfg.fps_display || m_visible) {
        ImGui::SetNextWindowPos(ImVec2(8, 8), ImGuiCond_Always);
        ImGui::SetNextWindowBgAlpha(cfg.overlay_opacity * 0.4f);
        ImGuiWindowFlags hudFlags =
            ImGuiWindowFlags_NoDecoration    |
            ImGuiWindowFlags_NoInputs        |
            ImGuiWindowFlags_AlwaysAutoResize|
            ImGuiWindowFlags_NoSavedSettings |
            ImGuiWindowFlags_NoFocusOnAppearing|
            ImGuiWindowFlags_NoNav;

        if (ImGui::Begin("##HUD", nullptr, hudFlags)) {
            SharedState* ss = GetSharedState();

            // FPS counter
            if (cfg.fps_display) {
                static float fps = 0.0f;
                static float lastTime = 0.0f;
                float now = ImGui::GetTime();
                if (now - lastTime > 0.5f) {
                    fps = ImGui::GetIO().Framerate;
                    lastTime = now;
                }

                // Color-code FPS
                ImVec4 fpsColor;
                if (fps >= 60)       fpsColor = ImVec4(0.2f, 1.0f, 0.4f, 1.0f); // Green
                else if (fps >= 30)  fpsColor = ImVec4(1.0f, 0.8f, 0.2f, 1.0f); // Yellow
                else                 fpsColor = ImVec4(1.0f, 0.3f, 0.3f, 1.0f); // Red

                ImGui::TextColored(fpsColor, "FPS: %.0f", fps);
            }

            // SGSR + FG status
            if (ss) {
                SharedStateLock l(&ss->lock);

                // SGSR status
                if (ss->sgsr_enabled || ss->sgsr_active) {
                    ImVec4 sgsrColor = ss->sgsr_active
                        ? ImVec4(0.2f, 1.0f, 0.4f, 1.0f)  // Green — DLSS path
                        : ImVec4(0.8f, 0.8f, 0.2f, 1.0f);  // Yellow — DXGI path
                    const char* sgsrText = ss->sgsr_active ? "SGSR: ON (DLSS)" : "SGSR: ON (Sharp)";
                    ImGui::TextColored(sgsrColor, "%s", sgsrText);
                } else {
                    ImGui::TextDisabled("SGSR: OFF");
                }

                // FG status
                if (ss->fg_mode > 1) {
                    ImGui::TextColored(
                        ImVec4(0.4f, 0.7f, 1.0f, 1.0f),
                        "FG: x%d", ss->fg_mode);
                }

                // Render resolution (if DLSS path active)
                if (ss->sgsr_active && ss->render_width > 0) {
                    ImGui::TextDisabled("%ux%u → %ux%u",
                        ss->render_width, ss->render_height,
                        ss->display_width, ss->display_height);
                }
            }
        }
        ImGui::End();
    }

    // ── Startup notification (fades out after 4 seconds) ──
    static float showTime = ImGui::GetTime();
    float elapsed = ImGui::GetTime() - showTime;
    if (elapsed < 4.0f) {
        float alpha = (elapsed < 3.0f) ? 0.85f : 0.85f * (1.0f - (elapsed - 3.0f));
        ImGui::SetNextWindowPos(ImVec2(width / 2.0f - 160, height - 60), ImGuiCond_Always);
        ImGui::SetNextWindowBgAlpha(alpha);
        ImGuiWindowFlags notifyFlags =
            ImGuiWindowFlags_NoDecoration     |
            ImGuiWindowFlags_NoInputs         |
            ImGuiWindowFlags_AlwaysAutoResize |
            ImGuiWindowFlags_NoSavedSettings  |
            ImGuiWindowFlags_NoFocusOnAppearing|
            ImGuiWindowFlags_NoNav;

        if (ImGui::Begin("##Notify", nullptr, notifyFlags)) {
            ImGui::TextColored(
                ImVec4(0.4f, 0.7f, 1.0f, alpha),
                "AdrenaProxy v2.0 loaded — Press HOME for settings");
        }
        ImGui::End();
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

    // Toggle overlay on hotkey
    if (msg == WM_KEYDOWN) {
        Config& cfg = GetConfig();
        if (wp == cfg.toggle_key) {
            // The actual toggle is handled by ProxySwapChain::StaticWndProc
            // which calls m_overlay->Toggle()
            return true;
        }
    }

    // Block input when overlay is visible (prevent game from receiving it)
    if (s_instance && s_instance->m_visible) {
        switch (msg) {
        case WM_MOUSEMOVE:
        case WM_LBUTTONDOWN: case WM_LBUTTONUP:
        case WM_RBUTTONDOWN: case WM_RBUTTONUP:
        case WM_MBUTTONDOWN: case WM_MBUTTONUP:
        case WM_MOUSEWHEEL: case WM_MOUSEHWHEEL:
        case WM_KEYDOWN: case WM_KEYUP:
        case WM_CHAR:
            return true; // Consume input
        }
    }
#endif
    return 0;
}

} // namespace adrena