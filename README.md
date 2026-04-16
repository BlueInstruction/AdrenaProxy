# AdrenaProxy

**Qualcomm SGSR Upscaling & Frame Generation — DirectX 11 / DirectX 12 / Vulkan**

AdrenaProxy is a `dxgi.dll` proxy mod that brings Qualcomm's Snapdragon Game Super Resolution (SGSR) upscaling and compute-based Frame Generation to **any** DirectX 11 or DirectX 12 game — including inside emulators like Winlator on Snapdragon/Adreno devices.

Inspired by OptiScaler and DLSS Enabler MFG, but powered by SGSR for Qualcomm/Adreno GPUs.

---

## ✨ Features

| Feature | Status | Details |
|---|---|---|
| **DirectX 11 Support** | ✅ Stable | Full swapchain hook with D3D11 compute |
| **DirectX 12 Support** | ✅ Stable | Full swapchain + command queue hook |
| **SGSR 1** — Spatial upscaling | ✅ Stable | 12-tap Lanczos + edge-adaptive sharpening |
| **SGSR 2** — Temporal upscaling | ⚠️ Experimental | TAAU-based, requires depth buffer |
| **Frame Generation x2/x3/x4** | ⚠️ Experimental | Compute-based interpolation |
| **In-game Overlay Menu** | ✅ Stable | ImGui-based, like OptiScaler |
| **Auto GPU Detection** | ✅ Stable | Adreno 6xx/7xx/8xx auto-tuning |
| **INI + Live Config** | ✅ Stable | Per-game settings + runtime changes |

---

## 🎮 How It Works

```
┌──────────────────────────────────────────────────┐
│  Game (DX11 or DX12)                             │
│  Renders to low-res backbuffer                   │
└─────────────────────┬────────────────────────────┘
                      ↓
┌──────────────────────────────────────────────────┐
│  AdrenaProxy dxgi.dll                            │
│  ┌────────────────────────────────────────────┐  │
│  │  GetBuffer → fake low-res render target    │  │
│  │  Present  → SGSR upscale (EASU + RCAS)    │  │
│  │          → Frame Gen interpolation         │  │
│  │          → ImGui overlay rendering         │  │
│  │          → Real Present                    │  │
│  └────────────────────────────────────────────┘  │
│  Supports: ID3D11Device + ID3D12Device           │
│  Compute:  D3D11 CS or D3D12 CS                 │
└─────────────────────┬────────────────────────────┘
                      ↓
┌──────────────────────────────────────────────────┐
│  Real dxgi.dll (System / DXVK / Wine)            │
│  → Vulkan / GPU                                  │
└──────────────────────────────────────────────────┘
```

---

## 📐 Quality Presets

| Preset | Render Scale | Use Case |
|---|---|---|
| Ultra Quality | 77% | Minimal quality loss |
| Quality | 67% | Best balance |
| Balanced | 59% | Smooth gameplay |
| Performance | 50% | Max FPS |
| Ultra Performance | 33% | Extreme FPS |

---

## 🎬 Frame Generation Modes

| Mode | Output Frames | Best For |
|---|---|---|
| x1 (Off) | 1:1 | No interpolation |
| x2 | 2× | Adreno 6xx/7xx |
| x3 | 3× | Adreno 7xx/8xx |
| x4 | 4× | Adreno 8xx only |

> ⚠️ Frame Generation is compute-based approximation. Quality is below DLSS 3 / FSR 3 FG.

---

## 🛠️ Installation

1. Download latest release from [Releases](../../releases)
2. Copy `dxgi.dll` + `adrena_proxy.ini` to game directory (next to `.exe`)
3. Edit `adrena_proxy.ini` or use in-game overlay
4. Press **Home** key to open overlay menu

---

## 🔧 Building

### Prerequisites
- CMake 3.20+
- MinGW-w64 (x86_64-w64-mingw32) or MSVC 2022

### MinGW (Linux Cross-Compile)
```bash
git clone https://github.com/BlueInstruction/AdrenaProxy.git
cd AdrenaProxy && mkdir build && cd build
cmake .. -G Ninja \
  -DCMAKE_SYSTEM_NAME=Windows \
  -DCMAKE_C_COMPILER=x86_64-w64-mingw32-gcc \
  -DCMAKE_CXX_COMPILER=x86_64-w64-mingw32-g++
make -j$(nproc)
```

### MSVC (Windows)
```bash
git clone https://github.com/BlueInstruction/AdrenaProxy.git
cd AdrenaProxy && mkdir build && cd build
cmake .. -G "Visual Studio 17 2022" -A x64
cmake --build . --config Release
```

---

## ⚙️ Configuration

See `adrena_proxy.ini` for all options. Key settings:

```ini
[SGSR]
enabled=1
mode=sgsr1           ; off / sgsr1 / sgsr2
quality=quality      ; ultra_quality / quality / balanced / performance / ultra_performance
sharpness=0.80

[FrameGeneration]
mode=x1              ; x1 / x2 / x3 / x4
motion_quality=medium; low / medium / high

[Overlay]
enabled=1
toggle_key=HOME
```

---

## 🖥️ Overlay Menu

Press **Home** (configurable) to toggle the in-game overlay:

- **SGSR Tab**: Enable/disable, mode, quality, sharpness
- **Frame Gen Tab**: x1/x2/x3/x4, motion quality
- **Display Tab**: FPS counter, overlay opacity
- **Advanced Tab**: GPU info, log level, vsync

All changes apply instantly — no game restart needed.

---

## Compatibility

| Environment | Status |
|---|---|
| Windows on Snapdragon (native DX12) | ✅ Primary target |
| Winlator (DXVK + VKD3D + Adreno) | ✅ Supported |
| Wine + DXVK (Linux) | ✅ Supported |
| Windows + NVIDIA/AMD/Intel | ✅ SGSR works universally |

---

## Credits

- **Qualcomm SGSR** — BSD-3-Clause License
- **ImGui** — MIT License
- **OptiScaler** — Architecture inspiration
- **DXVK** — Interop interfaces

## License

MIT License (AdrenaProxy code) / BSD-3-Clause (SGSR shaders)

## Disclaimer

Not affiliated with Qualcomm Technologies, Inc.
SGSR™ and Snapdragon™ are trademarks of Qualcomm Technologies, Inc.
