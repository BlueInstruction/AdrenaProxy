#pragma once
#include <string>
#include <cstdint>

struct IDXGISwapChain1;
struct ID3D11Device;
struct ID3D11DeviceContext;
struct ID3D12Device;
struct ID3D12CommandQueue;

namespace adrena {

class OverlayMenu {
public:
    OverlayMenu();
    ~OverlayMenu();

    void Init(HWND hwnd);
    void Shutdown();

    void RenderFrame11(ID3D11Device* dev, ID3D11DeviceContext* ctx, IDXGISwapChain1* sc);
    void RenderFrame12(ID3D12Device* dev, ID3D12CommandQueue* queue, IDXGISwapChain1* sc);

    void ToggleVisibility();
    bool IsVisible() const { return m_visible; }
    void SetVisible(bool v) { m_visible = v; }

    void HandleInput(UINT msg, WPARAM wParam, LPARAM lParam);
    void UpdateFPS();

    float GetFPS() const { return m_fps; }

private:
    void BuildUI();
    void BuildSGSRTab();
    void BuildFGTab();
    void BuildDisplayTab();
    void BuildAdvancedTab();

    bool m_initialized = false;
    bool m_visible = false;
    bool m_d3d12Initialized = false;
    bool m_d3d11Initialized = false;

    // FPS tracking
    float m_fps = 0.0f;
    uint64_t m_frameCount = 0;
    double m_lastFpsTime = 0.0;
    uint64_t m_lastFpsFrames = 0;

    // D3D12 overlay resources
    struct ID3D12DescriptorHeap* m_rtvHeap12 = nullptr;
    struct ID3D12DescriptorHeap* m_srvHeap12 = nullptr;
    struct ID3D12CommandAllocator* m_cmdAlloc12 = nullptr;
    struct ID3D12GraphicsCommandList* m_cmdList12 = nullptr;
    struct ID3D12Fence* m_fence12 = nullptr;
    UINT64 m_fenceValue = 0;
    void* m_fenceEvent = nullptr;
    struct ID3D12Resource* m_fontTexture12 = nullptr;
};

OverlayMenu& GetOverlayMenu();

} // namespace adrena