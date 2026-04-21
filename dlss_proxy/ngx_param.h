#pragma once
#include <d3d11.h>
#include <d3d12.h>
#include <cstdint>
#include <unordered_map>
#include <string>

namespace adrena {

// ────────────────────────────────────────────────────────────────────────
//  NVSDK_NGX_Parameter — public NVIDIA NGX abstract class.
//
//  The NGX Helpers SDK expands macros like
//     NVSDK_NGX_Parameter_SetD3d12Resource(params, key, val)
//  into virtual method calls:
//     ((NVSDK_NGX_Parameter*)params)->Set(key, val);
//
//  The vtable layout below must stay in lock-step with NVIDIA's public
//  header <nvsdk_ngx_params.h>.  Changing order, removing overloads, or
//  inserting new virtuals will break every shipping DLSS game.
// ────────────────────────────────────────────────────────────────────────
class NVSDK_NGX_Parameter {
public:
    virtual ~NVSDK_NGX_Parameter() = default;

    virtual void Set(const char* Name, unsigned long long Value) = 0;
    virtual void Set(const char* Name, float              Value) = 0;
    virtual void Set(const char* Name, double             Value) = 0;
    virtual void Set(const char* Name, unsigned int       Value) = 0;
    virtual void Set(const char* Name, int                Value) = 0;
    virtual void Set(const char* Name, ID3D11Resource*    Value) = 0;
    virtual void Set(const char* Name, ID3D12Resource*    Value) = 0;
    virtual void Set(const char* Name, const void*        Value) = 0;
    virtual void Set(const char* Name, void*              Value) = 0;

    virtual int Get(const char* Name, unsigned long long* OutValue) const = 0;
    virtual int Get(const char* Name, float*              OutValue) const = 0;
    virtual int Get(const char* Name, double*             OutValue) const = 0;
    virtual int Get(const char* Name, unsigned int*       OutValue) const = 0;
    virtual int Get(const char* Name, int*                OutValue) const = 0;
    virtual int Get(const char* Name, ID3D11Resource**    OutValue) const = 0;
    virtual int Get(const char* Name, ID3D12Resource**    OutValue) const = 0;
    virtual int Get(const char* Name, void**              OutValue) const = 0;

    virtual void Reset() = 0;
};

// ────────────────────────────────────────────────────────────────────────
//  NGXParameter — concrete implementation backing the proxy.
//
//  Exposes BOTH:
//    • The C++ virtual Set/Get interface (what DLSS games call via the
//      Helpers macros).
//    • Flat C-style GetValue*/SetValue* accessors used by the internal
//      NVNGX_* and DLSS_* exports in dlss_main.cpp.
// ────────────────────────────────────────────────────────────────────────
class NGXParameter : public NVSDK_NGX_Parameter {
public:
    NGXParameter();
    ~NGXParameter() override;

    // ── Virtual (NVIDIA ABI) ──────────────────────────────────────
    void Set(const char* k, unsigned long long v) override;
    void Set(const char* k, float              v) override;
    void Set(const char* k, double             v) override;
    void Set(const char* k, unsigned int       v) override;
    void Set(const char* k, int                v) override;
    void Set(const char* k, ID3D11Resource*    v) override;
    void Set(const char* k, ID3D12Resource*    v) override;
    void Set(const char* k, const void*        v) override;
    void Set(const char* k, void*              v) override;

    int Get(const char* k, unsigned long long* out) const override;
    int Get(const char* k, float*              out) const override;
    int Get(const char* k, double*             out) const override;
    int Get(const char* k, unsigned int*       out) const override;
    int Get(const char* k, int*                out) const override;
    int Get(const char* k, ID3D11Resource**    out) const override;
    int Get(const char* k, ID3D12Resource**    out) const override;
    int Get(const char* k, void**              out) const override;

    void Reset() override;

    // ── Flat C-style API (used by internal call paths) ────────────
    int SetValueUI32(const char* key, uint32_t val);
    int SetValueI32(const char* key, int32_t val);
    int SetValueF32(const char* key, float val);
    int SetValueF64(const char* key, double val);
    int SetValueUI64(const char* key, uint64_t val);
    int SetValuePtr(const char* key, void* val);
    int SetValueD3D11Res(const char* key, ID3D11Resource* val);
    int SetValueD3D12Res(const char* key, ID3D12Resource* val);

    int GetValueUI32(const char* key, uint32_t* val) const;
    int GetValueI32(const char* key, int32_t* val) const;
    int GetValueF32(const char* key, float* val) const;
    int GetValueF64(const char* key, double* val) const;
    int GetValueUI64(const char* key, uint64_t* val) const;
    int GetValuePtr(const char* key, void** val) const;
    int GetValueD3D11Res(const char* key, ID3D11Resource** val) const;
    int GetValueD3D12Res(const char* key, ID3D12Resource** val) const;

    // Lookup helper — tries several common key-name variants the way
    // different game engines spell DLSS parameters.
    ID3D12Resource* FindD3D12ByAny(const char* const* keys, int nkeys) const;

private:
    std::unordered_map<std::string, uint32_t>        m_ui32;
    std::unordered_map<std::string, int32_t>         m_i32;
    std::unordered_map<std::string, float>           m_f32;
    std::unordered_map<std::string, double>          m_f64;
    std::unordered_map<std::string, uint64_t>        m_ui64;
    std::unordered_map<std::string, void*>           m_ptr;
    std::unordered_map<std::string, ID3D11Resource*> m_d3d11;
    std::unordered_map<std::string, ID3D12Resource*> m_d3d12;
};

} // namespace adrena
