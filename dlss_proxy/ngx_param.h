#pragma once
#include <d3d12.h>
#include <cstdint>
#include <unordered_map>
#include <string>

namespace adrena {

// Minimal fake NVSDK_NGX_Parameter implementation.
// Stores key-value pairs for all types that games typically use.
// Supports both the C++ virtual interface and C API accessor exports.
class NGXParameter {
public:
    NGXParameter();
    ~NGXParameter();

    // C-style API (exported as flat functions by dlss.dll)
    int SetValueUI32(const char* key, uint32_t val);
    int SetValueI32(const char* key, int32_t val);
    int SetValueF32(const char* key, float val);
    int SetValueF64(const char* key, double val);
    int SetValueUI64(const char* key, uint64_t val);
    int SetValuePtr(const char* key, void* val);
    int SetValueD3D12Res(const char* key, ID3D12Resource* val);

    int GetValueUI32(const char* key, uint32_t* val);
    int GetValueI32(const char* key, int32_t* val);
    int GetValueF32(const char* key, float* val);
    int GetValueF64(const char* key, double* val);
    int GetValueUI64(const char* key, uint64_t* val);
    int GetValuePtr(const char* key, void** val);
    int GetValueD3D12Res(const char* key, ID3D12Resource** val);

    void Reset();

private:
    std::unordered_map<std::string, uint32_t>       m_ui32;
    std::unordered_map<std::string, int32_t>        m_i32;
    std::unordered_map<std::string, float>           m_f32;
    std::unordered_map<std::string, double>          m_f64;
    std::unordered_map<std::string, uint64_t>        m_ui64;
    std::unordered_map<std::string, void*>           m_ptr;
    std::unordered_map<std::string, ID3D12Resource*> m_d3d12;
};

} // namespace adrena