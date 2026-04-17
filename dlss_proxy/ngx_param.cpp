#include "ngx_param.h"

namespace adrena {

NGXParameter::NGXParameter() {}
NGXParameter::~NGXParameter() { Reset(); }

int NGXParameter::SetValueUI32(const char* key, uint32_t val) {
    if (!key) return -1; m_ui32[key] = val; return 0;
}
int NGXParameter::SetValueI32(const char* key, int32_t val) {
    if (!key) return -1; m_i32[key] = val; return 0;
}
int NGXParameter::SetValueF32(const char* key, float val) {
    if (!key) return -1; m_f32[key] = val; return 0;
}
int NGXParameter::SetValueF64(const char* key, double val) {
    if (!key) return -1; m_f64[key] = val; return 0;
}
int NGXParameter::SetValueUI64(const char* key, uint64_t val) {
    if (!key) return -1; m_ui64[key] = val; return 0;
}
int NGXParameter::SetValuePtr(const char* key, void* val) {
    if (!key) return -1; m_ptr[key] = val; return 0;
}
int NGXParameter::SetValueD3D12Res(const char* key, ID3D12Resource* val) {
    if (!key) return -1; m_d3d12[key] = val; return 0;
}

int NGXParameter::GetValueUI32(const char* key, uint32_t* val) {
    if (!key || !val) return -1;
    auto it = m_ui32.find(key);
    if (it == m_ui32.end()) return -1;
    *val = it->second; return 0;
}
int NGXParameter::GetValueI32(const char* key, int32_t* val) {
    if (!key || !val) return -1;
    auto it = m_i32.find(key);
    if (it == m_i32.end()) return -1;
    *val = it->second; return 0;
}
int NGXParameter::GetValueF32(const char* key, float* val) {
    if (!key || !val) return -1;
    auto it = m_f32.find(key);
    if (it == m_f32.end()) return -1;
    *val = it->second; return 0;
}
int NGXParameter::GetValueF64(const char* key, double* val) {
    if (!key || !val) return -1;
    auto it = m_f64.find(key);
    if (it == m_f64.end()) return -1;
    *val = it->second; return 0;
}
int NGXParameter::GetValueUI64(const char* key, uint64_t* val) {
    if (!key || !val) return -1;
    auto it = m_ui64.find(key);
    if (it == m_ui64.end()) return -1;
    *val = it->second; return 0;
}
int NGXParameter::GetValuePtr(const char* key, void** val) {
    if (!key || !val) return -1;
    auto it = m_ptr.find(key);
    if (it == m_ptr.end()) return -1;
    *val = it->second; return 0;
}
int NGXParameter::GetValueD3D12Res(const char* key, ID3D12Resource** val) {
    if (!key || !val) return -1;
    auto it = m_d3d12.find(key);
    if (it == m_d3d12.end()) return -1;
    *val = it->second; return 0;
}

void NGXParameter::Reset() {
    m_ui32.clear(); m_i32.clear(); m_f32.clear();
    m_f64.clear(); m_ui64.clear(); m_ptr.clear(); m_d3d12.clear();
}

} // namespace adrena