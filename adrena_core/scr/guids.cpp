// Single TU that defines INITGUID before including DXGI / D3D12 / Direct3D
// headers so the IID_* symbols get real storage in exactly one object file.
//
// Required because some toolchains (notably MinGW's libdxguid.a) do not ship
// definitions for newer interface IIDs (ID3D12DescriptorHeap, IDXGIFactory4,
// etc.). Without this, every target linking adrena_core hits
// "undefined reference to IID_ID3D12Resource" and similar at link time.

#define INITGUID

#include <windows.h>
#include <initguid.h>
#include <dxgi.h>
#include <dxgi1_2.h>
#include <dxgi1_3.h>
#include <dxgi1_4.h>
#include <dxgi1_5.h>
#include <dxgi1_6.h>
#include <d3d11.h>
#include <d3d12.h>
#include <d3dcompiler.h>

// Intentionally empty translation unit body. The above headers emit the
// IID_* definitions because INITGUID is defined.
