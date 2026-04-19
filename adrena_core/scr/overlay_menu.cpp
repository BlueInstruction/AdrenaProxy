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
#include "adrena_core/hack.h"
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
#endif

#include <d3d12.h>
#include <dxgi1_4.h>

// ────────────────────────────────────────────────────────
// Palette — deep charcoal + electric orange + cool whites
// ────────────────────────────────────────────────────────
//
//  BG_DEEP    #0C0D12   window background
//  BG_MID     #13141C   child panels, nav
//  BG_LIGHT   #1C1E2B   hover / frame
//  ORANGE     #FF6B00   primary accent
//  ORANGE_DIM #C45200   secondary / inactive
//  AMBER      #FFB347   warning / highlight
//  CYAN       #00D4FF   FG indicator
//  GREEN      #22DD66   active / good
//  RED        #FF3D3D   bad performance
//  YELLOW     #FFD600   caution
//  TEXT_HI    #E8EAF2   primary text
//  TEXT_LO    #5A5E78   disabled / hint

#define COL_BG_DEEP    ImVec4(0.047f, 0.051f, 0.071f, 1.00f)
#define COL_BG_MID     ImVec4(0.074f, 0.078f, 0.110f, 1.00f)
#define COL_BG_LIGHT   ImVec4(0.110f, 0.118f, 0.169f, 1.00f)
#define COL_BG_HOVER   ImVec4(0.140f, 0.150f, 0.210f, 1.00f)
#define COL_ORANGE     ImVec4(1.000f, 0.420f, 0.000f, 1.00f)
#define COL_ORANGE_DIM ImVec4(0.769f, 0.322f, 0.000f, 1.00f)
#define COL_ORANGE_A   ImVec4(1.000f, 0.420f, 0.000f, 0.18f)
#define COL_AMBER      ImVec4(1.000f, 0.702f, 0.278f, 1.00f)
#define COL_CYAN       ImVec4(0.000f, 0.831f, 1.000f, 1.00f)
#define COL_GREEN      ImVec4(0.133f, 0.867f, 0.400f, 1.00f)
#define COL_RED        ImVec4(1.000f, 0.239f, 0.239f, 1.00f)
#define COL_YELLOW     ImVec4(1.000f, 0.839f, 0.000f, 1.00f)
#define COL_TEXT_HI    ImVec4(0.910f, 0.918f, 0.945f, 1.00f)
#define COL_TEXT_MID   ImVec4(0.620f, 0.635f, 0.710f, 1.00f)
#define COL_TEXT_LO    ImVec4(0.353f, 0.369f, 0.471f, 1.00f)
#define COL_BORDER     ImVec4(0.200f, 0.212f, 0.310f, 0.70f)
#define COL_SEP        ImVec4(0.180f, 0.190f, 0.275f, 1.00f)

namespace adrena {

// ────────────────────────────────────────────────────────
// Destructor
// ────────────────────────────────────────────────────────

OverlayMenu::~OverlayMenu() { Shutdown(); }

// ────────────────────────────────────────────────────────
// Double-tap detection for HUD layout toggle
// ────────────────────────────────────────────────────────

void OverlayMenu::OnToggleKey() {
    // HOME key = toggle menu visibility only.
    // HUD layout is changed via the Display settings combo.
    m_visible = !m_visible;
    AD_LOG_I("Menu toggled: %s", m_visible ? "visible" : "hidden");
}

// ────────────────────────────────────────────────────────
// Win32 Init — context + font + theme
// ────────────────────────────────────────────────────────

bool OverlayMenu::InitWin32(HWND hwnd) {
#ifdef ADRENA_OVERLAY_ENABLED
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.IniFilename  = nullptr;
    io.LogFilename  = nullptr;

    // ── Hack font 14px ──
    ImFontConfig fontCfg;
    fontCfg.FontDataOwnedByAtlas = false;
    io.FontDefault = io.Fonts->AddFontFromMemoryTTF(
        (void*)hack_font_data, (int)hack_font_size, 14.0f, &fontCfg);

    // ── Style ──
    ImGui::StyleColorsDark();
    ImGuiStyle& s = ImGui::GetStyle();
    s.WindowRounding    = 10.0f;
    s.ChildRounding     =  7.0f;
    s.FrameRounding     =  5.0f;
    s.PopupRounding     =  7.0f;
    s.GrabRounding      =  4.0f;
    s.TabRounding       =  5.0f;
    s.ScrollbarRounding = 10.0f;
    s.WindowPadding     = ImVec2(0, 0);   // manual padding in children
    s.FramePadding      = ImVec2(10, 5);
    s.ItemSpacing       = ImVec2(10, 8);
    s.ItemInnerSpacing  = ImVec2(7, 4);
    s.WindowBorderSize  = 1.0f;
    s.FrameBorderSize   = 0.0f;
    s.GrabMinSize       = 11.0f;
    s.ScrollbarSize     = 10.0f;

    ImVec4* c = s.Colors;
    c[ImGuiCol_WindowBg]             = COL_BG_DEEP;
    c[ImGuiCol_ChildBg]              = COL_BG_MID;
    c[ImGuiCol_PopupBg]              = ImVec4(0.08f, 0.08f, 0.12f, 0.97f);
    c[ImGuiCol_TitleBg]              = COL_BG_DEEP;
    c[ImGuiCol_TitleBgActive]        = COL_BG_DEEP;
    c[ImGuiCol_TitleBgCollapsed]     = COL_BG_DEEP;
    c[ImGuiCol_FrameBg]              = COL_BG_LIGHT;
    c[ImGuiCol_FrameBgHovered]       = COL_BG_HOVER;
    c[ImGuiCol_FrameBgActive]        = ImVec4(0.17f, 0.19f, 0.28f, 1.0f);
    c[ImGuiCol_Button]               = ImVec4(0.14f, 0.15f, 0.22f, 1.0f);
    c[ImGuiCol_ButtonHovered]        = ImVec4(0.22f, 0.24f, 0.34f, 1.0f);
    c[ImGuiCol_ButtonActive]         = COL_ORANGE_DIM;
    c[ImGuiCol_CheckMark]            = COL_ORANGE;
    c[ImGuiCol_SliderGrab]           = COL_ORANGE;
    c[ImGuiCol_SliderGrabActive]     = COL_AMBER;
    c[ImGuiCol_Header]               = COL_ORANGE_A;
    c[ImGuiCol_HeaderHovered]        = ImVec4(1.0f, 0.42f, 0.0f, 0.28f);
    c[ImGuiCol_HeaderActive]         = ImVec4(1.0f, 0.42f, 0.0f, 0.40f);
    c[ImGuiCol_Separator]            = COL_SEP;
    c[ImGuiCol_SeparatorHovered]     = COL_ORANGE_DIM;
    c[ImGuiCol_SeparatorActive]      = COL_ORANGE;
    c[ImGuiCol_Tab]                  = COL_BG_MID;
    c[ImGuiCol_TabHovered]           = COL_BG_HOVER;
    c[ImGuiCol_TabActive]            = COL_BG_LIGHT;
    c[ImGuiCol_ScrollbarBg]          = ImVec4(0.04f, 0.04f, 0.06f, 0.6f);
    c[ImGuiCol_ScrollbarGrab]        = ImVec4(0.22f, 0.24f, 0.35f, 0.9f);
    c[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.32f, 0.35f, 0.50f, 1.0f);
    c[ImGuiCol_ScrollbarGrabActive]  = COL_ORANGE_DIM;
    c[ImGuiCol_Text]                 = COL_TEXT_HI;
    c[ImGuiCol_TextDisabled]         = COL_TEXT_LO;
    c[ImGuiCol_Border]               = COL_BORDER;
    c[ImGuiCol_BorderShadow]         = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_ResizeGrip]           = ImVec4(1.0f, 0.42f, 0.0f, 0.15f);
    c[ImGuiCol_ResizeGripHovered]    = ImVec4(1.0f, 0.42f, 0.0f, 0.40f);
    c[ImGuiCol_ResizeGripActive]     = COL_ORANGE;

    ImGui_ImplWin32_Init(hwnd);
    AD_LOG_I("Overlay Win32 initialized");
    return true;
#else
    return false;
#endif
}

// ────────────────────────────────────────────────────────
// DX12 Init
// ────────────────────────────────────────────────────────

bool OverlayMenu::InitDX12(ID3D12Device* device, DXGI_FORMAT rtvFormat) {
#ifdef ADRENA_DX12_OVERLAY
    if (m_initialized) return true;
    if (!device) return false;
    m_device = device;

    D3D12_DESCRIPTOR_HEAP_DESC rtvDesc{};
    rtvDesc.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    rtvDesc.NumDescriptors = 1;
    HRESULT hr = device->CreateDescriptorHeap(&rtvDesc, IID_ID3D12DescriptorHeap, (void**)&m_rtvHeap);
    if (FAILED(hr)) { AD_LOG_E("Overlay: RTV heap failed"); return false; }

    D3D12_DESCRIPTOR_HEAP_DESC srvDesc{};
    srvDesc.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    srvDesc.NumDescriptors = 1;
    srvDesc.Flags          = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    hr = device->CreateDescriptorHeap(&srvDesc, IID_ID3D12DescriptorHeap, (void**)&m_srvHeap);
    if (FAILED(hr)) { AD_LOG_E("Overlay: SRV heap failed"); return false; }

    hr = device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_ID3D12CommandAllocator, (void**)&m_cmdAlloc);
    if (FAILED(hr)) { AD_LOG_E("Overlay: CmdAlloc failed"); return false; }

    hr = device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, m_cmdAlloc, nullptr, IID_ID3D12GraphicsCommandList, (void**)&m_cmdList);
    if (FAILED(hr)) { AD_LOG_E("Overlay: CmdList failed"); return false; }
    m_cmdList->Close();

    hr = device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_ID3D12Fence, (void**)&m_fence);
    if (FAILED(hr)) { AD_LOG_E("Overlay: Fence failed"); return false; }
    m_fenceEvent = CreateEventA(nullptr, FALSE, FALSE, nullptr);
    if (!m_fenceEvent) { AD_LOG_E("Overlay: FenceEvent failed"); return false; }

    ImGui_ImplDX12_Init(device, 1, rtvFormat, m_srvHeap,
        m_srvHeap->GetCPUDescriptorHandleForHeapStart(),
        m_srvHeap->GetGPUDescriptorHandleForHeapStart());

    m_initialized = true;
    AD_LOG_I("Overlay DX12 backend initialized");
    return true;
#else
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
    if (m_cmdList)    { m_cmdList->Release();      m_cmdList    = nullptr; }
    if (m_cmdAlloc)   { m_cmdAlloc->Release();     m_cmdAlloc   = nullptr; }
    if (m_fence)      { m_fence->Release();        m_fence      = nullptr; }
    if (m_rtvHeap)    { m_rtvHeap->Release();      m_rtvHeap    = nullptr; }
    if (m_srvHeap)    { m_srvHeap->Release();      m_srvHeap    = nullptr; }
    if (m_fenceEvent) { CloseHandle(m_fenceEvent); m_fenceEvent = nullptr; }
    m_device = nullptr; m_initialized = false;
    AD_LOG_I("Overlay shut down");
#endif
}

// ────────────────────────────────────────────────────────
// Render
// ────────────────────────────────────────────────────────

void OverlayMenu::Render(ID3D12CommandQueue* cmdQueue,
                         ID3D12Resource* backbuffer,
                         uint32_t width, uint32_t height) {
#ifdef ADRENA_DX12_OVERLAY
    if (!m_initialized || !backbuffer || !cmdQueue) return;
    if (m_fence && m_fenceVal > 0 && m_fence->GetCompletedValue() < m_fenceVal) {
        m_fence->SetEventOnCompletion(m_fenceVal, m_fenceEvent);
        WaitForSingleObject(m_fenceEvent, INFINITE);
    }
    ImGui_ImplDX12_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();

    if (m_visible) BuildUI();
    RenderHUD((int)width, (int)height);

    ImGui::Render();
    m_cmdAlloc->Reset();
    m_cmdList->Reset(m_cmdAlloc, nullptr);

    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource   = backbuffer;
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
    barrier.Transition.StateAfter  = D3D12_RESOURCE_STATE_RENDER_TARGET;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    m_cmdList->ResourceBarrier(1, &barrier);

    D3D12_RENDER_TARGET_VIEW_DESC rtvDesc{};
    rtvDesc.Format        = DXGI_FORMAT_R8G8B8A8_UNORM;
    rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
    m_device->CreateRenderTargetView(backbuffer, &rtvDesc,
        m_rtvHeap->GetCPUDescriptorHandleForHeapStart());
    D3D12_CPU_DESCRIPTOR_HANDLE rtvH = m_rtvHeap->GetCPUDescriptorHandleForHeapStart();
    m_cmdList->OMSetRenderTargets(1, &rtvH, FALSE, nullptr);
    ID3D12DescriptorHeap* heaps[] = { m_srvHeap };
    m_cmdList->SetDescriptorHeaps(1, heaps);
    ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), m_cmdList);

    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
    barrier.Transition.StateAfter  = D3D12_RESOURCE_STATE_PRESENT;
    m_cmdList->ResourceBarrier(1, &barrier);
    m_cmdList->Close();
    ID3D12CommandList* lists[] = { m_cmdList };
    cmdQueue->ExecuteCommandLists(1, lists);
    m_fenceVal++;
    cmdQueue->Signal(m_fence, m_fenceVal);
#endif
}

// ────────────────────────────────────────────────────────
// Helper: draw a colored pill badge (inline label)
// ────────────────────────────────────────────────────────

void OverlayMenu::DrawStatusBadge(const char* text, ImVec4 color) {
#ifdef ADRENA_OVERLAY_ENABLED
    ImVec2 pos  = ImGui::GetCursorScreenPos();
    ImVec2 size = ImGui::CalcTextSize(text);
    float  pad  = 6.0f;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec4 bg = ImVec4(color.x * 0.25f, color.y * 0.25f, color.z * 0.25f, 0.90f);
    dl->AddRectFilled(
        ImVec2(pos.x - pad, pos.y - 2),
        ImVec2(pos.x + size.x + pad, pos.y + size.y + 2),
        ImGui::ColorConvertFloat4ToU32(bg), 4.0f);
    dl->AddRect(
        ImVec2(pos.x - pad, pos.y - 2),
        ImVec2(pos.x + size.x + pad, pos.y + size.y + 2),
        ImGui::ColorConvertFloat4ToU32(ImVec4(color.x, color.y, color.z, 0.50f)), 4.0f);
    ImGui::TextColored(color, "%s", text);
    ImGui::SetCursorScreenPos(ImVec2(pos.x + size.x + pad * 2 + 2, pos.y));
    ImGui::SameLine(0, 0);
    ImGui::Dummy(ImVec2(0, size.y)); // advance cursor properly
    ImGui::SameLine();
#endif
}

// ────────────────────────────────────────────────────────
// Helper: sidebar nav item
// ────────────────────────────────────────────────────────

void OverlayMenu::DrawNavItem(const char* icon, const char* label, int index) {
#ifdef ADRENA_OVERLAY_ENABLED
    bool selected = (m_navPage == index);
    float w = ImGui::GetContentRegionAvail().x;

    ImVec2 pos = ImGui::GetCursorScreenPos();
    ImDrawList* dl = ImGui::GetWindowDrawList();

    // active left-bar indicator
    if (selected) {
        dl->AddRectFilled(
            ImVec2(pos.x, pos.y + 4),
            ImVec2(pos.x + 3, pos.y + 32),
            ImGui::ColorConvertFloat4ToU32(COL_ORANGE), 2.0f);
    }

    ImVec4 bgCol = selected ? COL_BG_LIGHT : ImVec4(0, 0, 0, 0);
    ImGui::PushStyleColor(ImGuiCol_Button, bgCol);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, COL_BG_HOVER);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,  COL_BG_LIGHT);
    ImGui::PushStyleColor(ImGuiCol_Text, selected ? COL_ORANGE : COL_TEXT_MID);
    ImGui::PushStyleVar(ImGuiStyleVar_ButtonTextAlign, ImVec2(0.08f, 0.5f));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 0.0f);

    char buf[64];
    snprintf(buf, sizeof(buf), "  %s  %s##nav%d", icon, label, index);
    if (ImGui::Button(buf, ImVec2(w, 36)))
        m_navPage = index;

    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(4);
#endif
}

// ────────────────────────────────────────────────────────
// Page helpers
// ────────────────────────────────────────────────────────

static void SectionHeader(const char* title) {
#ifdef ADRENA_OVERLAY_ENABLED
    ImGui::Spacing();
    ImGui::PushStyleColor(ImGuiCol_Text, COL_TEXT_LO);
    ImGui::Text("%s", title);
    ImGui::PopStyleColor();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 p = ImGui::GetCursorScreenPos();
    float w  = ImGui::GetContentRegionAvail().x;
    dl->AddLine(ImVec2(p.x, p.y), ImVec2(p.x + w, p.y),
        ImGui::ColorConvertFloat4ToU32(COL_SEP));
    ImGui::Dummy(ImVec2(0, 4));
#endif
}

static void SmallLabel(const char* text) {
#ifdef ADRENA_OVERLAY_ENABLED
    ImGui::PushStyleColor(ImGuiCol_Text, COL_TEXT_LO);
    ImGui::TextUnformatted(text);
    ImGui::PopStyleColor();
#endif
}

// ────────────────────────────────────────────────────────
// Page: SGSR
// ────────────────────────────────────────────────────────

void OverlayMenu::DrawPageSGSR() {
#ifdef ADRENA_OVERLAY_ENABLED
    Config& cfg     = GetConfig();
    SharedState* ss = GetSharedState();

    ImGui::Dummy(ImVec2(0, 2));

    // ── Active Upscaler Plugin ──
    SectionHeader("ACTIVE UPSCALER");
    {
        // Maps to plugin IDs: sgsr1, sgsr2, fsr2, xess
        const char* plugins[] = { "SGSR1 (Spatial)", "SGSR2 (Temporal)", "FSR2 (AMD)", "XeSS (Intel)" };
        // Index: 0=sgsr1, 1=sgsr2, 2=fsr2, 3=xess
        static int pluginIdx = (cfg.sgsr_mode == SGSRMode::SGSR2) ? 1 : 0;
        ImGui::SetNextItemWidth(-1);
        if (ImGui::Combo("##plugin", &pluginIdx, plugins, 4)) {
            switch (pluginIdx) {
            case 0: cfg.sgsr_mode = SGSRMode::SGSR1; cfg.enabled = true; break;
            case 1: cfg.sgsr_mode = SGSRMode::SGSR2; cfg.enabled = true; break;
            case 2: cfg.sgsr_mode = SGSRMode::SGSR1; cfg.enabled = true; break; // FSR2 — routed via plugin
            case 3: cfg.sgsr_mode = SGSRMode::SGSR1; cfg.enabled = true; break; // XeSS — routed via plugin
            }
            if (ss) { SharedStateLock l(&ss->lock); ss->sgsr_enabled = cfg.enabled; }
        }
        if (pluginIdx >= 2) {
            ImGui::TextColored(COL_AMBER, "Requires plugin DLL in plugins/ folder");
        }
    }

    // ── SGSR Mode ──
    SectionHeader("SGSR MODE");

    const char* modes[] = { "Off", "SGSR1 — Spatial", "SGSR2 — Temporal" };
    int mode = cfg.enabled ? (int)cfg.sgsr_mode : 0;
    ImGui::SetNextItemWidth(-1);
    if (ImGui::Combo("##mode", &mode, modes, 3)) {
        cfg.enabled   = (mode != 0);
        cfg.sgsr_mode = (mode == 0) ? SGSRMode::Off : (SGSRMode)mode;
        if (ss) { SharedStateLock l(&ss->lock); ss->sgsr_enabled = cfg.enabled; }
    }

    if (cfg.sgsr_mode == SGSRMode::SGSR2) {
        ImGui::TextColored(COL_AMBER, "SGSR2 needs depth + motion (DLSS games)");
    }

    // ── Quality Preset ──
    SectionHeader("QUALITY PRESET");
    {
        const char* qualities[] = {
            "Ultra Quality  (77%%)",
            "Quality  (67%%)",
            "Balanced  (59%%)",
            "Performance  (50%%)",
            "Ultra Performance  (33%%)"
        };
        const float qscales[] = { 0.77f, 0.67f, 0.59f, 0.50f, 0.33f };
        int qIdx = (int)cfg.quality;
        ImGui::SetNextItemWidth(-1);
        if (ImGui::Combo("##quality", &qIdx, qualities, 5)) {
            cfg.quality = (Quality)qIdx;
            cfg.custom_scale = qscales[qIdx];
            cfg.ApplyRenderScale();
            if (ss) { SharedStateLock l(&ss->lock); ss->render_scale = cfg.render_scale; }
        }
    }

    // ── Resolution ──
    SectionHeader("RENDER RESOLUTION");
    {
        const char* resolutions[] = {
            "Native",
            "2560x1440",
            "1920x1080",
            "1600x900",
            "1440x810",
            "1366x768",
            "1280x720",
            "1152x648",
            "1024x576",
            "960x540",
            "854x480",
            "800x450",
            "768x432",
            "720x405",
            "640x360",
            "576x324",
            "480x270",
            "Custom..."
        };
        const float rscales[] = {
            1.00f, 0.75f, 0.5625f, 0.469f, 0.5625f, 0.533f, 0.50f,
            0.45f, 0.40f, 0.375f, 0.333f, 0.3125f, 0.30f, 0.281f,
            0.25f, 0.225f, 0.1875f, 0.0f
        };
        static int resIdx = 17; // Custom
        ImGui::SetNextItemWidth(-1);
        if (ImGui::Combo("##res", &resIdx, resolutions, 18)) {
            if (resIdx < 17) {
                cfg.custom_scale = rscales[resIdx];
                cfg.ApplyRenderScale();
                if (ss) { SharedStateLock l(&ss->lock); ss->render_scale = cfg.render_scale; }
            }
        }
    }

    // ── Custom Scale Slider ──
    SectionHeader("CUSTOM SCALE");
    float scale = cfg.GetRenderScale();
    ImGui::SetNextItemWidth(-1);
    if (ImGui::SliderFloat("##scale", &scale, 0.10f, 1.0f, "%.0f%%")) {
        cfg.custom_scale = scale; cfg.ApplyRenderScale();
        if (ss) { SharedStateLock l(&ss->lock); ss->render_scale = cfg.render_scale; }
    }
    SmallLabel("Drag to set any render scale");

    // ── Sharpness ──
    SectionHeader("SHARPNESS");
    ImGui::SetNextItemWidth(-1);
    if (ImGui::SliderFloat("##sharp", &cfg.sharpness, 0.0f, 2.0f, "%.2f")) {
        if (ss) { SharedStateLock l(&ss->lock); ss->sharpness = cfg.sharpness; }
    }
    SmallLabel("0.0 = none   0.8 = default   2.0 = maximum");

    // ── Status card ──
    SectionHeader("STATUS");
    if (ss) {
        SharedStateLock l(&ss->lock);
        if (ss->sgsr_active) {
            ImGui::TextColored(COL_GREEN, "DLSS Path — %ux%u  →  %ux%u  (%.0f%%)",
                ss->render_width, ss->render_height,
                ss->display_width, ss->display_height,
                ss->render_scale * 100.0f);
        } else if (cfg.enabled) {
            ImGui::TextColored(COL_AMBER, "DXGI Sharpening  (%.0f%%)", cfg.render_scale * 100.0f);
        } else {
            ImGui::TextColored(COL_TEXT_LO, "Disabled");
        }
    }
#endif
}

// ────────────────────────────────────────────────────────
// Page: Frame Generation
// ────────────────────────────────────────────────────────

void OverlayMenu::DrawPageFrameGen() {
#ifdef ADRENA_OVERLAY_ENABLED
    Config& cfg     = GetConfig();
    SharedState* ss = GetSharedState();

    ImGui::Dummy(ImVec2(0, 2));
    SectionHeader("MULTIPLIER");

    const char* fgModes[] = { "Off (x1)", "x2  — +1 frame", "x3  — +2 frames", "x4  — +3 frames" };
    int fg = (int)cfg.fg_mode;
    ImGui::SetNextItemWidth(-1);
    if (ImGui::Combo("##fg", &fg, fgModes, 4)) {
        cfg.fg_mode = (FGMode)fg;
        if (ss) { SharedStateLock l(&ss->lock); ss->fg_mode = (int32_t)cfg.fg_mode + 1; }
    }

    SectionHeader("FPS THRESHOLD");
    ImGui::SetNextItemWidth(-1);
    if (ImGui::SliderInt("##thr", &cfg.fps_threshold, 0, 240)) {}
    SmallLabel(cfg.fps_threshold == 0 ? "Auto-disable: Off (always active)" :
               "Auto-disable above threshold FPS");

    SectionHeader("ABOUT");
    ImGui::PushStyleColor(ImGuiCol_Text, COL_TEXT_MID);
    ImGui::TextWrapped(
        "Pure Extra Present — inserts empty Present() calls to "
        "stimulate the Vulkan pipeline on Turnip/Adreno. "
        "Zero GPU compute. No backbuffer index shift, "
        "so overlay rendering never flickers.");
    ImGui::PopStyleColor();

    if (ss) {
        SharedStateLock l(&ss->lock);
        SectionHeader("ACTIVE");
        ImGui::TextColored(ss->fg_mode > 1 ? COL_CYAN : COL_TEXT_LO,
            "x%d  (%d interpolated frames per real frame)",
            ss->fg_mode, ss->fg_mode - 1);
    }
#endif
}

// ────────────────────────────────────────────────────────
// Page: Display
// ────────────────────────────────────────────────────────

void OverlayMenu::DrawPageDisplay() {
#ifdef ADRENA_OVERLAY_ENABLED
    Config& cfg     = GetConfig();
    SharedState* ss = GetSharedState();

    ImGui::Dummy(ImVec2(0, 2));
    SectionHeader("HUD");

    if (ImGui::Checkbox("Show Performance HUD", &cfg.fps_display))
        if (ss) { SharedStateLock l(&ss->lock); ss->fps_display = cfg.fps_display; }

    if (ImGui::Checkbox("Show Banner on Startup", &cfg.overlay_enabled))
        if (ss) { SharedStateLock l(&ss->lock); ss->overlay_visible = cfg.overlay_enabled; }

    SectionHeader("APPEARANCE");
    ImGui::SetNextItemWidth(-1);
    ImGui::SliderFloat("##opacity", &cfg.overlay_opacity, 0.3f, 1.0f, "Menu Opacity  %.2f");

    SectionHeader("KEY BINDINGS");
    ImGui::PushStyleColor(ImGuiCol_Text, COL_TEXT_MID);
    ImGui::BulletText("HOME               Toggle menu");
    ImGui::BulletText("All changes        Apply instantly");
    ImGui::PopStyleColor();

    SectionHeader("HUD ORIENTATION");
    {
        SharedState* hss = GetSharedState();
        bool isHoriz = true;
        if (hss) { SharedStateLock l(&hss->lock); isHoriz = hss->hud_horizontal; }
        const char* orientations[] = { "Horizontal Bar", "Vertical Stack" };
        int oriIdx = isHoriz ? 0 : 1;
        ImGui::SetNextItemWidth(-1);
        if (ImGui::Combo("##hudori", &oriIdx, orientations, 2)) {
            if (hss) {
                SharedStateLock l(&hss->lock);
                hss->hud_horizontal = (oriIdx == 0);
            }
            AD_LOG_I("HUD orientation changed to %s", orientations[oriIdx]);
        }
    }
#endif
}

// ────────────────────────────────────────────────────────
// Page: Advanced
// ────────────────────────────────────────────────────────

void OverlayMenu::DrawPageAdvanced() {
#ifdef ADRENA_OVERLAY_ENABLED
    Config& cfg     = GetConfig();
    SharedState* ss = GetSharedState();

    ImGui::Dummy(ImVec2(0, 2));
    SectionHeader("GPU");
    if (ss) {
        SharedStateLock l(&ss->lock);
        ImGui::TextColored(COL_TEXT_HI, "%s", ss->is_adreno ? "Qualcomm Adreno" : "Unknown");
        if (ss->is_adreno && ss->adreno_tier > 0)
            ImGui::TextColored(COL_TEXT_MID, "Tier  %dxx", ss->adreno_tier);
        ImGui::TextColored(COL_TEXT_MID, "DLSS Proxy  %s",
            ss->sgsr_active ? "Active" : "Inactive");
    }
    {
        MEMORYSTATUSEX m = {}; m.dwLength = sizeof(m);
        if (GlobalMemoryStatusEx(&m)) {
            DWORDLONG used  = (m.ullTotalPhys - m.ullAvailPhys) / (1024*1024);
            DWORDLONG total = m.ullTotalPhys / (1024*1024);
            ImGui::TextColored(COL_TEXT_MID, "RAM  %llu / %llu MB  (%lu%%)", used, total, m.dwMemoryLoad);
        }
    }

    SectionHeader("RENDERING");
    const char* vsyncOpts[] = { "Off", "On", "Adaptive" };
    int vsIdx = (cfg.vsync < 0 || cfg.vsync > 2) ? 0 : cfg.vsync;
    ImGui::SetNextItemWidth(-1);
    if (ImGui::Combo("VSync##adv", &vsIdx, vsyncOpts, 3)) cfg.vsync = vsIdx;

    ImGui::SetNextItemWidth(-1);
    ImGui::SliderInt("Frame Queue##adv", &cfg.max_frame_queue, 1, 5);
    SmallLabel("Lower = less input lag   Higher = smoother");

    SectionHeader("ACTIONS");
    ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(1.0f, 0.42f, 0.0f, 0.20f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(1.0f, 0.42f, 0.0f, 0.35f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(1.0f, 0.42f, 0.0f, 0.55f));
    ImGui::PushStyleColor(ImGuiCol_Text,          COL_ORANGE);
    if (ImGui::Button("Save Config  →  INI", ImVec2(-1, 30))) {
        cfg.Save(GetConfigPath());
        AD_LOG_I("Config saved to INI");
    }
    ImGui::PopStyleColor(4);

    ImGui::Spacing();
    ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.14f, 0.15f, 0.22f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, COL_BG_HOVER);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(0.20f, 0.22f, 0.32f, 1.0f));
    if (ImGui::Button("Reset All to Defaults", ImVec2(-1, 30))) {
        cfg = Config(); cfg.ApplyRenderScale();
        if (ss) {
            SharedStateLock l(&ss->lock);
            ss->fg_mode = 1; ss->overlay_visible = true; ss->fps_display = true;
            ss->sharpness = cfg.sharpness; ss->render_scale = cfg.render_scale;
            ss->sgsr_enabled = false;
        }
        AD_LOG_I("Config reset to defaults");
    }
    ImGui::PopStyleColor(3);
#endif
}

// ────────────────────────────────────────────────────────
// BuildUI — left sidebar + content panel
// ────────────────────────────────────────────────────────

void OverlayMenu::BuildUI() {
#ifdef ADRENA_OVERLAY_ENABLED
    // ── Classic lightweight tab menu — minimal GPU/CPU overhead ──
    ImGui::SetNextWindowSize(ImVec2(420, 340), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowBgAlpha(0.94f);

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10, 8));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 6.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);

    if (!ImGui::Begin("AdrenaProxy v2.0", &m_visible, ImGuiWindowFlags_NoCollapse)) {
        ImGui::End();
        ImGui::PopStyleVar(3);
        return;
    }

    ImGui::Separator();

    if (ImGui::BeginTabBar("##MainTabs")) {
        if (ImGui::BeginTabItem("SGSR"))      { DrawPageSGSR();     ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("Frame Gen"))  { DrawPageFrameGen(); ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("Display"))    { DrawPageDisplay();  ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("Advanced"))   { DrawPageAdvanced(); ImGui::EndTabItem(); }
        ImGui::EndTabBar();
    }

    ImGui::End();
    ImGui::PopStyleVar(3);
#endif
}

// ────────────────────────────────────────────────────────
// RenderHUD — MangoHud-style horizontal bar
// ────────────────────────────────────────────────────────
//
//  ┌──────┬────────┬──────────────┬────────┬───────────────────┬──────────┐
//  │ FPS  │ FTIME  │ SGSR         │ FG     │ GPU               │ RAM      │
//  │ 120  │ 8.3 ms │ DLSS 720→4K │ x2     │ Adreno 750        │ 5.1/8 GB │
//  └──────┴────────┴──────────────┴────────┴───────────────────┴──────────┘
//
// Each segment has its own background pill so it reads like MangoHud columns.

void OverlayMenu::RenderHUD(int width, int height) {
#ifdef ADRENA_OVERLAY_ENABLED
    Config& cfg = GetConfig();
    if (!cfg.fps_display && !m_visible) return;

    // ── Throttled metric sampling ──
    static float   s_fps = 0.0f, s_ft = 0.0f, s_lastSmooth = 0.0f;
    static DWORDLONG s_ramUsed = 0, s_ramTotal = 0;
    static float   s_lastSys = 0.0f;
    float now = (float)ImGui::GetTime();

    if (now - s_lastSmooth > 0.5f) {
        float rawFps = ImGui::GetIO().Framerate;
        // ImGui counts ALL Present() calls including FG extra presents.
        // Divide by FG multiplier to get the real game FPS.
        SharedState* fgSs = GetSharedState();
        int fgDiv = 1;
        if (fgSs) { SharedStateLock l(&fgSs->lock); fgDiv = fgSs->fg_mode; }
        if (fgDiv < 1) fgDiv = 1;
        s_fps = rawFps / (float)fgDiv;
        s_ft  = (s_fps > 0.0f) ? (1000.0f / s_fps) : 0.0f;
        s_lastSmooth = now;
    }
    if (now - s_lastSys > 1.5f) {
        MEMORYSTATUSEX m = {}; m.dwLength = sizeof(m);
        if (GlobalMemoryStatusEx(&m)) {
            s_ramUsed  = (m.ullTotalPhys - m.ullAvailPhys) / (1024*1024);
            s_ramTotal = m.ullTotalPhys / (1024*1024);
        }
        s_lastSys = now;
    }

    float ramUsedGB  = (float)s_ramUsed  / 1024.0f;
    float ramTotalGB = (float)s_ramTotal / 1024.0f;

    // FPS color — MangoHud v0.7+ gradient style
    //  90+ = bright green, 60-89 = lime, 45-59 = yellow, 30-44 = orange, <30 = red
    ImVec4 fpsCol;
    if      (s_fps >= 90.0f) fpsCol = ImVec4(0.18f, 0.80f, 0.44f, 1.0f);  // #2ECC71
    else if (s_fps >= 60.0f) fpsCol = ImVec4(0.64f, 0.85f, 0.47f, 1.0f);  // #A3D977
    else if (s_fps >= 45.0f) fpsCol = ImVec4(0.95f, 0.77f, 0.06f, 1.0f);  // #F1C40F
    else if (s_fps >= 30.0f) fpsCol = ImVec4(0.90f, 0.49f, 0.13f, 1.0f);  // #E67E22
    else                      fpsCol = ImVec4(0.91f, 0.30f, 0.24f, 1.0f);  // #E74C3C

    // Frame time color — inverse thresholds matching MangoHud
    ImVec4 ftCol;
    if      (s_ft <= 11.1f) ftCol = ImVec4(0.18f, 0.80f, 0.44f, 1.0f);  // <11ms = green
    else if (s_ft <= 16.7f) ftCol = ImVec4(0.64f, 0.85f, 0.47f, 1.0f);  // <16.7ms = lime
    else if (s_ft <= 22.2f) ftCol = ImVec4(0.95f, 0.77f, 0.06f, 1.0f);  // <22ms = yellow
    else if (s_ft <= 33.3f) ftCol = ImVec4(0.90f, 0.49f, 0.13f, 1.0f);  // <33ms = orange
    else                     ftCol = ImVec4(0.91f, 0.30f, 0.24f, 1.0f);  // >33ms = red

    // ── Check HUD layout preference ──
    bool hudHoriz = true;
    {
        SharedState* hss = GetSharedState();
        if (hss) { SharedStateLock l(&hss->lock); hudHoriz = hss->hud_horizontal; }
    }

    // ── HUD window — pinned top-left, no decorations ──
    if (!cfg.fps_display) goto startup_notif;

    // Dispatch to vertical HUD if toggled
    if (!hudHoriz) {
        RenderHUDVertical((int)ImGui::GetIO().DisplaySize.x,
                          (int)ImGui::GetIO().DisplaySize.y);
        goto startup_notif;
    }

    ImGui::SetNextWindowPos(ImVec2(8, 8), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowBgAlpha(0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,   ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);

    // HUD is moveable — user can drag it to any position.
    // NoTitleBar + NoResize but NOT NoInputs = draggable.
    if (ImGui::Begin("##HUD", nullptr,
            ImGuiWindowFlags_NoTitleBar         |
            ImGuiWindowFlags_NoResize           |
            ImGuiWindowFlags_AlwaysAutoResize   |
            ImGuiWindowFlags_NoSavedSettings    |
            ImGuiWindowFlags_NoFocusOnAppearing |
            ImGuiWindowFlags_NoNav              |
            ImGuiWindowFlags_NoBackground)) {

        ImDrawList* dl  = ImGui::GetWindowDrawList();
        ImVec2      cur = ImGui::GetCursorScreenPos();
        float       h   = 28.0f;
        float       x   = cur.x;
        float       y   = cur.y;

        // Helper lambdas (C++11 style)
        auto PillBg = [&](float px, float pw, ImVec4 bg) {
            dl->AddRectFilled(ImVec2(px, y), ImVec2(px + pw, y + h),
                ImGui::ColorConvertFloat4ToU32(bg), 5.0f);
        };
        auto PillBorder = [&](float px, float pw, ImVec4 border) {
            dl->AddRect(ImVec2(px, y), ImVec2(px + pw, y + h),
                ImGui::ColorConvertFloat4ToU32(border), 5.0f, 0, 1.0f);
        };

        // Build segments
        struct Seg { char top[32]; char bot[32]; ImVec4 topCol; ImVec4 bg; };
        Seg segs[6];
        int nSeg = 0;

        // FPS
        snprintf(segs[nSeg].top, 32, "FPS");
        snprintf(segs[nSeg].bot, 32, "%.0f", s_fps);
        segs[nSeg].topCol = COL_TEXT_LO;
        segs[nSeg].bg     = ImVec4(fpsCol.x*0.15f, fpsCol.y*0.15f, fpsCol.z*0.15f, 0.92f);
        nSeg++;

        // Frame time
        snprintf(segs[nSeg].top, 32, "FTIME");
        snprintf(segs[nSeg].bot, 32, "%.1fms", s_ft);
        segs[nSeg].topCol = COL_TEXT_LO;
        segs[nSeg].bg     = ImVec4(0.08f, 0.08f, 0.14f, 0.92f);
        nSeg++;

        // SGSR
        {
            SharedState* ss = GetSharedState();
            snprintf(segs[nSeg].top, 32, "SGSR");
            segs[nSeg].bg = ImVec4(0.08f, 0.08f, 0.14f, 0.92f);
            segs[nSeg].topCol = COL_TEXT_LO;
            if (ss) {
                SharedStateLock l(&ss->lock);
                if (ss->sgsr_active) {
                    snprintf(segs[nSeg].bot, 32, "DLSS %.0f%%", ss->render_scale * 100.0f);
                    segs[nSeg].bg = ImVec4(0.05f, 0.20f, 0.10f, 0.92f);
                } else if (ss->sgsr_enabled) {
                    snprintf(segs[nSeg].bot, 32, "SHARP");
                    segs[nSeg].bg = ImVec4(0.18f, 0.14f, 0.04f, 0.92f);
                } else {
                    snprintf(segs[nSeg].bot, 32, "OFF");
                }
            } else { snprintf(segs[nSeg].bot, 32, "OFF"); }
        }
        nSeg++;

        // FG
        {
            SharedState* ss = GetSharedState();
            snprintf(segs[nSeg].top, 32, "FG");
            segs[nSeg].topCol = COL_TEXT_LO;
            segs[nSeg].bg     = ImVec4(0.08f, 0.08f, 0.14f, 0.92f);
            if (ss) {
                SharedStateLock l(&ss->lock);
                if (ss->fg_mode > 1) {
                    snprintf(segs[nSeg].bot, 32, "x%d", ss->fg_mode);
                    segs[nSeg].bg = ImVec4(0.04f, 0.14f, 0.22f, 0.92f);
                } else { snprintf(segs[nSeg].bot, 32, "OFF"); }
            } else { snprintf(segs[nSeg].bot, 32, "OFF"); }
        }
        nSeg++;

        // GPU name
        {
            SharedState* ss = GetSharedState();
            snprintf(segs[nSeg].top, 32, "GPU");
            snprintf(segs[nSeg].bot, 32, "Adreno");
            segs[nSeg].topCol = COL_TEXT_LO;
            segs[nSeg].bg     = ImVec4(0.08f, 0.08f, 0.14f, 0.92f);
            if (ss) {
                SharedStateLock l(&ss->lock);
                if (ss->is_adreno && ss->adreno_tier > 0)
                    snprintf(segs[nSeg].bot, 32, "A-%dxx", ss->adreno_tier * 100);
            }
        }
        nSeg++;

        // RAM
        snprintf(segs[nSeg].top, 32, "RAM");
        snprintf(segs[nSeg].bot, 32, "%.1f/%.0fG", ramUsedGB, ramTotalGB);
        segs[nSeg].topCol = COL_TEXT_LO;
        segs[nSeg].bg     = ImVec4(0.08f, 0.08f, 0.14f, 0.92f);
        nSeg++;

        // ── Measure and draw segments ──
        float padX = 10.0f, gap = 2.0f;
        float totalW = 0.0f;
        float segW[6] = {};
        for (int i = 0; i < nSeg; i++) {
            float tw = ImGui::CalcTextSize(segs[i].top).x;
            float bw = ImGui::CalcTextSize(segs[i].bot).x;
            segW[i]  = (tw > bw ? tw : bw) + padX * 2.0f;
            totalW  += segW[i] + (i > 0 ? gap : 0.0f);
        }

        // Draw background capsule behind everything
        dl->AddRectFilled(ImVec2(x - 2, y - 1), ImVec2(x + totalW + 2, y + h + 1),
            ImGui::ColorConvertFloat4ToU32(ImVec4(0, 0, 0, 0)), 6.0f);

        float cx = x;
        for (int i = 0; i < nSeg; i++) {
            float sw = segW[i];
            // pill background
            PillBg(cx, sw, segs[i].bg);
            // border: only first and last get outer border; others get inner divider
            if (i == 0 || i == nSeg - 1)
                PillBorder(cx, sw, ImVec4(0.25f, 0.27f, 0.40f, 0.60f));
            else
                dl->AddLine(ImVec2(cx + sw, y + 3), ImVec2(cx + sw, y + h - 3),
                    ImGui::ColorConvertFloat4ToU32(ImVec4(0.22f, 0.24f, 0.35f, 0.80f)));

            // label top
            float lw = ImGui::CalcTextSize(segs[i].top).x;
            dl->AddText(ImVec2(cx + (sw - lw) * 0.5f, y + 2),
                ImGui::ColorConvertFloat4ToU32(segs[i].topCol),
                segs[i].top);

            // value bottom
            ImVec4 valCol = (i == 0) ? fpsCol : (i == 1) ? ftCol : COL_TEXT_HI;
            float vw = ImGui::CalcTextSize(segs[i].bot).x;
            dl->AddText(ImVec2(cx + (sw - vw) * 0.5f, y + 13),
                ImGui::ColorConvertFloat4ToU32(valCol),
                segs[i].bot);

            cx += sw + gap;
        }

        // Advance the window cursor so ImGui knows the size
        ImGui::Dummy(ImVec2(totalW, h));
    }
    ImGui::End();
    ImGui::PopStyleVar(3);

startup_notif:
    // ── Startup banner (bottom-center, fades after 4 s) ──
    static float s_showTime = now;
    float elapsed = now - s_showTime;
    if (elapsed < 4.0f) {
        float alpha = (elapsed < 2.8f) ? 1.0f : (1.0f - (elapsed - 2.8f) / 1.2f);

        float bw = 310.0f, bh = 30.0f;
        ImGui::SetNextWindowPos(ImVec2((float)width * 0.5f - bw * 0.5f, (float)height - bh - 12.0f),
            ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(bw, bh));
        ImGui::SetNextWindowBgAlpha(0.0f);

        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,    ImVec2(0, 0));

        if (ImGui::Begin("##Banner", nullptr,
                ImGuiWindowFlags_NoDecoration      |
                ImGuiWindowFlags_NoInputs           |
                ImGuiWindowFlags_NoSavedSettings    |
                ImGuiWindowFlags_NoFocusOnAppearing |
                ImGuiWindowFlags_NoNav              |
                ImGuiWindowFlags_NoBackground)) {

            ImDrawList* dl = ImGui::GetWindowDrawList();
            ImVec2 bp = ImGui::GetWindowPos();

            dl->AddRectFilled(ImVec2(bp.x, bp.y), ImVec2(bp.x + bw, bp.y + bh),
                ImGui::ColorConvertFloat4ToU32(ImVec4(0.06f, 0.06f, 0.12f, 0.88f * alpha)), 8.0f);
            dl->AddRect(ImVec2(bp.x, bp.y), ImVec2(bp.x + bw, bp.y + bh),
                ImGui::ColorConvertFloat4ToU32(ImVec4(1.0f, 0.42f, 0.0f, 0.35f * alpha)), 8.0f);

            // left: logo
            float tx = bp.x + 14.0f, ty = bp.y + 7.0f;
            dl->AddText(ImVec2(tx, ty),
                ImGui::ColorConvertFloat4ToU32(ImVec4(1.0f, 0.42f, 0.0f, alpha)), "ADRENAP");
            float lw = ImGui::CalcTextSize("ADRENAP").x;
            dl->AddText(ImVec2(tx + lw, ty),
                ImGui::ColorConvertFloat4ToU32(ImVec4(0.9f, 0.92f, 0.96f, alpha)), "ROXY v2.0");

            // right: hint
            const char* hint = "HOME for settings";
            float hw = ImGui::CalcTextSize(hint).x;
            dl->AddText(ImVec2(bp.x + bw - hw - 14.0f, ty),
                ImGui::ColorConvertFloat4ToU32(ImVec4(0.55f, 0.58f, 0.70f, alpha * 0.85f)), hint);

            ImGui::Dummy(ImVec2(bw, bh));
        }
        ImGui::End();
        ImGui::PopStyleVar(2);
    }
#endif
}

// ────────────────────────────────────────────────────────
// RenderHUDVertical — stacked vertical FPS/stats panel
// ────────────────────────────────────────────────────────

void OverlayMenu::RenderHUDVertical(int width, int height) {
#ifdef ADRENA_OVERLAY_ENABLED
    Config& cfg = GetConfig();
    if (!cfg.fps_display && !m_visible) return;

    // ── Throttled metrics (shared with horizontal HUD via statics) ──
    float fps = ImGui::GetIO().Framerate;
    float ft  = (fps > 0.0f) ? (1000.0f / fps) : 0.0f;

    ImVec4 fpsCol;
    if      (fps >= 60.0f) fpsCol = COL_GREEN;
    else if (fps >= 30.0f) fpsCol = COL_YELLOW;
    else                    fpsCol = COL_RED;

    ImGui::SetNextWindowPos(ImVec2(8, 8), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowBgAlpha(0.88f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10, 8));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 6.0f);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.05f, 0.05f, 0.08f, 0.92f));
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(1.0f, 0.42f, 0.0f, 0.30f));

    // Vertical HUD is also moveable — drag to reposition.
    if (ImGui::Begin("##HUDv", nullptr,
            ImGuiWindowFlags_NoTitleBar         |
            ImGuiWindowFlags_NoResize           |
            ImGuiWindowFlags_AlwaysAutoResize   |
            ImGuiWindowFlags_NoSavedSettings    |
            ImGuiWindowFlags_NoFocusOnAppearing |
            ImGuiWindowFlags_NoNav)) {

        // Title
        ImGui::TextColored(COL_ORANGE, "ADRENAPROXY");
        ImGui::Separator();

        // FPS
        ImGui::TextColored(COL_TEXT_LO, "FPS");
        ImGui::SameLine(70);
        ImGui::TextColored(fpsCol, "%.0f", fps);

        // Frame time
        ImGui::TextColored(COL_TEXT_LO, "FTIME");
        ImGui::SameLine(70);
        ImVec4 ftCol = (ft <= 16.7f) ? COL_GREEN : (ft <= 33.3f) ? COL_YELLOW : COL_RED;
        ImGui::TextColored(ftCol, "%.1f ms", ft);

        // SGSR status
        SharedState* ss = GetSharedState();
        ImGui::TextColored(COL_TEXT_LO, "SGSR");
        ImGui::SameLine(70);
        if (ss) {
            SharedStateLock l(&ss->lock);
            if (ss->sgsr_active)
                ImGui::TextColored(COL_GREEN, "DLSS %.0f%%", ss->render_scale * 100.0f);
            else if (ss->sgsr_enabled)
                ImGui::TextColored(COL_AMBER, "SHARP");
            else
                ImGui::TextColored(COL_TEXT_LO, "OFF");
        } else {
            ImGui::TextColored(COL_TEXT_LO, "OFF");
        }

        // FG
        ImGui::TextColored(COL_TEXT_LO, "FG");
        ImGui::SameLine(70);
        if (ss) {
            SharedStateLock l(&ss->lock);
            if (ss->fg_mode > 1)
                ImGui::TextColored(COL_CYAN, "x%d", ss->fg_mode);
            else
                ImGui::TextColored(COL_TEXT_LO, "OFF");
        } else {
            ImGui::TextColored(COL_TEXT_LO, "OFF");
        }

        // GPU
        ImGui::TextColored(COL_TEXT_LO, "GPU");
        ImGui::SameLine(70);
        if (ss) {
            SharedStateLock l(&ss->lock);
            if (ss->is_adreno && ss->adreno_tier > 0)
                ImGui::TextColored(COL_TEXT_HI, "A-%dxx", ss->adreno_tier * 100);
            else
                ImGui::TextColored(COL_TEXT_MID, "Adreno");
        } else {
            ImGui::TextColored(COL_TEXT_MID, "Unknown");
        }

        // RAM
        MEMORYSTATUSEX m = {}; m.dwLength = sizeof(m);
        if (GlobalMemoryStatusEx(&m)) {
            float used  = (float)(m.ullTotalPhys - m.ullAvailPhys) / (1024*1024*1024.0f);
            float total = (float)m.ullTotalPhys / (1024*1024*1024.0f);
            ImGui::TextColored(COL_TEXT_LO, "RAM");
            ImGui::SameLine(70);
            ImGui::TextColored(COL_TEXT_HI, "%.1f/%.0fG", used, total);
        }
    }
    ImGui::End();
    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar(3);
#endif
}

// ────────────────────────────────────────────────────────
// WndProc
// ────────────────────────────────────────────────────────

LRESULT CALLBACK OverlayMenu::WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
#ifdef ADRENA_OVERLAY_ENABLED
    if (ImGui_ImplWin32_WndProcHandler(hwnd, msg, wp, lp)) return true;
#endif
    return 0;
}

} // namespace adrena
