#include "ngx_param.h"

namespace adrena {

NGXParameter::NGXParameter() {}
NGXParameter::~NGXParameter() { Reset(); }

// ──────────────────────────────────────────────────────────────────
//  NVIDIA ABI virtual methods — forward to the flat SetValue*/GetValue*.
// ──────────────────────────────────────────────────────────────────
void NGXParameter::Set(const char* k, unsigned long long v) { SetValueUI64(k, v); }
void NGXParameter::Set(const char* k, float              v) { SetValueF32 (k, v); }
void NGXParameter::Set(const char* k, double             v) { SetValueF64 (k, v); }
void NGXParameter::Set(const char* k, unsigned int       v) { SetValueUI32(k, v); }
void NGXParameter::Set(const char* k, int                v) { SetValueI32 (k, v); }
void NGXParameter::Set(const char* k, ID3D11Resource*    v) { SetValueD3D11Res(k, v); }
void NGXParameter::Set(const char* k, ID3D12Resource*    v) { SetValueD3D12Res(k, v); }
void NGXParameter::Set(const char* k, const void*        v) { SetValuePtr(k, const_cast<void*>(v)); }
void NGXParameter::Set(const char* k, void*              v) { SetValuePtr(k, v); }

int NGXParameter::Get(const char* k, unsigned long long* out) const { return GetValueUI64(k, out); }
int NGXParameter::Get(const char* k, float*              out) const { return GetValueF32 (k, out); }
int NGXParameter::Get(const char* k, double*             out) const { return GetValueF64 (k, out); }
int NGXParameter::Get(const char* k, unsigned int*       out) const { return GetValueUI32(k, out); }
int NGXParameter::Get(const char* k, int*                out) const { return GetValueI32 (k, out); }
int NGXParameter::Get(const char* k, ID3D11Resource**    out) const { return GetValueD3D11Res(k, out); }
int NGXParameter::Get(const char* k, ID3D12Resource**    out) const { return GetValueD3D12Res(k, out); }
int NGXParameter::Get(const char* k, void**              out) const { return GetValuePtr(k, out); }

// ──────────────────────────────────────────────────────────────────
//  Flat C-style API.
// ──────────────────────────────────────────────────────────────────
int NGXParameter::SetValueUI32(const char* k, uint32_t v)       { if (!k) return -1; m_ui32[k]  = v; return 0; }
int NGXParameter::SetValueI32 (const char* k, int32_t v)        { if (!k) return -1; m_i32[k]   = v; return 0; }
int NGXParameter::SetValueF32 (const char* k, float v)          { if (!k) return -1; m_f32[k]   = v; return 0; }
int NGXParameter::SetValueF64 (const char* k, double v)         { if (!k) return -1; m_f64[k]   = v; return 0; }
int NGXParameter::SetValueUI64(const char* k, uint64_t v)       { if (!k) return -1; m_ui64[k]  = v; return 0; }
int NGXParameter::SetValuePtr (const char* k, void* v)          { if (!k) return -1; m_ptr[k]   = v; return 0; }
int NGXParameter::SetValueD3D11Res(const char* k, ID3D11Resource* v) { if (!k) return -1; m_d3d11[k] = v; return 0; }
int NGXParameter::SetValueD3D12Res(const char* k, ID3D12Resource* v) { if (!k) return -1; m_d3d12[k] = v; return 0; }

int NGXParameter::GetValueUI32(const char* k, uint32_t* v) const {
    if (!k || !v) return -1;
    auto it = m_ui32.find(k); if (it == m_ui32.end()) return -1;
    *v = it->second; return 0;
}
int NGXParameter::GetValueI32(const char* k, int32_t* v) const {
    if (!k || !v) return -1;
    auto it = m_i32.find(k); if (it == m_i32.end()) return -1;
    *v = it->second; return 0;
}
int NGXParameter::GetValueF32(const char* k, float* v) const {
    if (!k || !v) return -1;
    auto it = m_f32.find(k); if (it == m_f32.end()) return -1;
    *v = it->second; return 0;
}
int NGXParameter::GetValueF64(const char* k, double* v) const {
    if (!k || !v) return -1;
    auto it = m_f64.find(k); if (it == m_f64.end()) return -1;
    *v = it->second; return 0;
}
int NGXParameter::GetValueUI64(const char* k, uint64_t* v) const {
    if (!k || !v) return -1;
    auto it = m_ui64.find(k); if (it == m_ui64.end()) return -1;
    *v = it->second; return 0;
}
int NGXParameter::GetValuePtr(const char* k, void** v) const {
    if (!k || !v) return -1;
    auto it = m_ptr.find(k); if (it == m_ptr.end()) return -1;
    *v = it->second; return 0;
}
int NGXParameter::GetValueD3D11Res(const char* k, ID3D11Resource** v) const {
    if (!k || !v) return -1;
    auto it = m_d3d11.find(k); if (it == m_d3d11.end()) return -1;
    *v = it->second; return 0;
}
int NGXParameter::GetValueD3D12Res(const char* k, ID3D12Resource** v) const {
    if (!k || !v) return -1;
    auto it = m_d3d12.find(k); if (it == m_d3d12.end()) return -1;
    *v = it->second; return 0;
}

ID3D12Resource* NGXParameter::FindD3D12ByAny(const char* const* keys, int nkeys) const {
    for (int i = 0; i < nkeys; ++i) {
        ID3D12Resource* r = nullptr;
        if (GetValueD3D12Res(keys[i], &r) == 0 && r) return r;
    }
    return nullptr;
}

void NGXParameter::Reset() {
    m_ui32.clear(); m_i32.clear(); m_f32.clear();
    m_f64.clear(); m_ui64.clear(); m_ptr.clear();
    m_d3d11.clear(); m_d3d12.clear();
}

} // namespace adrena
