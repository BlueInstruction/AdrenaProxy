// Shared fallback used by scaffold plugins when the real SDK runtime isn't
// present next to the game. Issues a CopyResource(output, color) so the
// host's integration continues to produce a visible frame (native-res, no
// upscaling). Real SDK integration replaces this call in execute().
#pragma once

#include <d3d12.h>

namespace adrena::plugin {

// Copies input_color -> output with the appropriate transitions. Caller
// must ensure the two resources are compatible (same dimensions for a
// passthrough; otherwise the game will see the unchanged output).
inline void FallbackCopy(ID3D12GraphicsCommandList* cl,
                         ID3D12Resource* src,
                         ID3D12Resource* dst) {
    if (!cl || !src || !dst) return;

    D3D12_RESOURCE_BARRIER pre[2]{};
    pre[0].Type                    = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    pre[0].Transition.pResource    = src;
    pre[0].Transition.StateBefore  = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    pre[0].Transition.StateAfter   = D3D12_RESOURCE_STATE_COPY_SOURCE;
    pre[0].Transition.Subresource  = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;

    pre[1].Type                    = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    pre[1].Transition.pResource    = dst;
    pre[1].Transition.StateBefore  = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    pre[1].Transition.StateAfter   = D3D12_RESOURCE_STATE_COPY_DEST;
    pre[1].Transition.Subresource  = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    cl->ResourceBarrier(2, pre);

    cl->CopyResource(dst, src);

    D3D12_RESOURCE_BARRIER post[2]{};
    post[0] = pre[0];
    post[0].Transition.StateBefore = pre[0].Transition.StateAfter;
    post[0].Transition.StateAfter  = pre[0].Transition.StateBefore;
    post[1] = pre[1];
    post[1].Transition.StateBefore = pre[1].Transition.StateAfter;
    post[1].Transition.StateAfter  = pre[1].Transition.StateBefore;
    cl->ResourceBarrier(2, post);
}

} // namespace adrena::plugin
