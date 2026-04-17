#pragma once
#include <windows.h>
#include <cstdint>
#include <dxgi.h>

struct IDXGISwapChain1;
struct ID3D11Device;
struct ID3D11DeviceContext;

#ifdef ADRENA_DX12_OVERLAY
struct ID3D12Device;
struct ID3D12CommandQueue;
struct ID3D12GraphicsCommandList;
struct ID3D12DescriptorHeap;
#endif

namespace adrena {

class OverlayMenu {
public:
    OverlayMenu(); ~OverlayMenu();
    void Init(HWND hwnd); void Shutdown();
    void RenderFrame11(ID3D11Device* dev, ID3D11DeviceContext* ctx, IDXGISwapChain1* sc);

#ifdef ADRENA_DX12_OVERLAY
    void InitDX12(ID3D12Device* dev, UINT bufferCount, DXGI_FORMAT format);
    void BeginDX12Frame();
    void EndDX12Frame(ID3D12GraphicsCommandList* cmdList);
#endif

    void ToggleVisibility(); bool IsVisible() const { return m_visible; }
    void HandleInput(UINT msg, WPARAM wParam, LPARAM lParam);
    void UpdateFPS(); float GetFPS() const { return m_fps; }
    void BuildUI();
private:
    void BuildSGSRTab(); void BuildFGTab(); void BuildDisplayTab(); void BuildAdvancedTab();
    bool m_initialized = false, m_visible = false, m_d3d11Initialized = false;

#ifdef ADRENA_DX12_OVERLAY
    bool m_d3d12Initialized = false;
    ID3D12DescriptorHeap* m_srvHeap = nullptr;
#endif

    float m_fps = 0.0f; uint64_t m_frameCount = 0; double m_lastFpsTime = 0.0; uint64_t m_lastFpsFrames = 0;
};

OverlayMenu& GetOverlayMenu();

} // namespace adrena
