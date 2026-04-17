#pragma once
#include <windows.h>
#include <cstdint>

struct ID3D12Device;
struct ID3D12GraphicsCommandList;
struct ID3D12CommandQueue;
struct IDXGISwapChain3;

namespace adrena {

class OverlayMenu {
public:
    OverlayMenu() = default;
    ~OverlayMenu();
    OverlayMenu(const OverlayMenu&) = delete;
    OverlayMenu& operator=(const OverlayMenu&) = delete;

    bool InitWin32(HWND hwnd);
    bool InitDX12(ID3D12Device* device, DXGI_FORMAT rtvFormat);
    void Shutdown();

    void Render(ID3D12CommandQueue* cmdQueue,
                ID3D12Resource* backbuffer,
                uint32_t width, uint32_t height);

    void Toggle() { m_visible = !m_visible; }
    bool IsVisible() const { return m_visible; }

    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);

private:
    void BuildUI();
    void RenderHUD(int width, int height);

    ID3D12Device*              m_device     = nullptr;
    ID3D12DescriptorHeap*      m_rtvHeap    = nullptr;
    ID3D12DescriptorHeap*      m_srvHeap    = nullptr;
    ID3D12CommandAllocator*    m_cmdAlloc   = nullptr;
    ID3D12GraphicsCommandList* m_cmdList    = nullptr;
    ID3D12Fence*               m_fence      = nullptr;
    uint64_t                   m_fenceVal   = 0;
    HANDLE                     m_fenceEvent = nullptr;
    bool m_visible     = false;
    bool m_initialized = false;
};

} // namespace adrena