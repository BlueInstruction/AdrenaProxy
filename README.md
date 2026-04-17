# AdrenaProxy v2.0
**Qualcomm SGSR Upscaling & Frame Generation — DirectX 12**

AdrenaProxy brings Qualcomm's Snapdragon Game Super Resolution (SGSR) upscaling and Frame Generation to any DLSS-compatible DirectX 12 game — including inside emulators like Winlator on Snapdragon/Adreno devices.

Inspired by OptiScaler and DLSS Enabler, powered by SGSR for Qualcomm GPUs.

---

## ✨ Features

| Feature | Status | Details |
|---|---|---|
| **SGSR 1 Upscaling** | ✅ Stable | Via fake NVNGX API (dlss.dll) |
| **SGSR 1 Sharpening** | ✅ Stable | Fallback for non-DLSS games |
| **Frame Generation x2/x3/x4** | ✅ Stable | Pure Extra Present (zero GPU overhead) |
| **In-game Overlay** | ✅ Stable | ImGui-based, HOME key toggle |
| **Auto GPU Detection** | ✅ Stable | Adreno 6xx/7xx/8xx auto-tuning |
| **Multiple Entry Points** | ✅ Stable | dxgi.dll, version.dll |
| **INI + Live Config** | ✅ Stable | Per-game settings + runtime changes |

---

## 🎮 How It Works

### DLSS-Compatible Games (Path A)

```
Game (DLSS support)
→ calls DLSS_GetOptimalSettings → we return lower resolution
→ game renders at lower resolution (real GPU savings!)
→ calls DLSS_Evaluate → we run SGSR to upscale to display resolution
→ dxgi.dll adds Frame Generation (Pure Extra Present)
→ dxgi.dll renders ImGui overlay
→ Real Present
```

### Non-DLSS Games (Path B)

```
Game (no DLSS)
→ renders normally at full resolution
→ dxgi.dll applies SGSR sharpening (post-process)
→ dxgi.dll adds Frame Generation (Pure Extra Present)
→ dxgi.dll renders ImGui overlay
→ Real Present
→ Early exit if everything OFF (zero overhead)
```

---

## 📐 Quality Presets (DLSS Games)

| Preset | Render Scale | Use Case |
|---|---|---|
| Ultra Quality | 77% | Minimal quality loss |
| Quality | 67% | Best balance |
| Balanced | 59% | Smooth gameplay |
| Performance | 50% | Max FPS |
| Ultra Performance | 33% | Extreme FPS |

---

## 🎬 Frame Generation

| Mode | Output | How It Works |
|---|---|---|
| x1 (Off) | 1× | No interpolation |
| x2 | 2× | 1 extra Present(0,0) per frame |
| x3 | 3× | 2 extra Presents |
| x4 | 4× | 3 extra Presents |

Pure Extra Present stimulates the Vulkan queue on Turnip/Adreno without
GPU compute overhead. It does **not** change the backbuffer index, which
permanently fixes the ImGui flickering issue.

---

## 🛠️ Installation

### Windows / Winlator

1. Download latest release from [Releases](../../releases)
2. Extract all files to game directory (next to `.exe`)
3. Run `AdrenaProxy Setup.bat`
4. Press **HOME** key in-game to open overlay

### Linux / Wine / Proton

```bash
chmod +x "AdrenaProxy Setup.sh"
./AdrenaProxy\ Setup.sh /path/to/game/directory
```

### Manual Installation

Copy files manually:

```
adrenaproxy_sgsr.dll  → dlss.dll
adrenaproxy_dxgi.dll  → dxgi.dll
adrena_proxy.ini      → same directory
```

### If dxgi.dll is already used by another mod

Use the `version.dll` entry point instead:

```
adrenaproxy_version.dll → version.dll
```

---

## 🔧 Building

### Prerequisites
- CMake 3.20+
- MSVC 2022 (recommended) or MinGW-w64

### MSVC (Windows — recommended)

```bash
git clone https://github.com/BlueInstruction/AdrenaProxy.git
cd AdrenaProxy && mkdir build && cd build
cmake .. -G "Visual Studio 17 2022" -A x64
cmake --build . --config Release
```

### MinGW (Linux Cross-Compile — compile check only)

```bash
git clone https://github.com/BlueInstruction/AdrenaProxy.git
cd AdrenaProxy && mkdir build && cd build
cmake .. -G Ninja \
  -DCMAKE_SYSTEM_NAME=Windows \
  -DCMAKE_C_COMPILER=x86_64-w64-mingw32-gcc \
  -DCMAKE_CXX_COMPILER=x86_64-w64-mingw32-g++
cmake --build . --config Release
```

---

## ⚙️ Configuration

See `adrena_proxy.ini` for all options. Most settings can be changed
at runtime via the in-game overlay (HOME key).

---

## 🖥️ Overlay

Press **HOME** to toggle:

- **SGSR Tab**: Enable/disable, quality, sharpness
- **Frame Gen Tab**: x1/x2/x3/x4, FPS threshold
- **Display Tab**: FPS counter, opacity

---

## 📁 File Reference

| Distribution File | Installed As | Purpose |
|---|---|---|
| `adrenaproxy_sgsr.dll` | `dlss.dll` | SGSR upscaler via NVNGX API |
| `adrenaproxy_dxgi.dll` | `dxgi.dll` | SwapChain proxy + FG + overlay |
| `adrenaproxy_version.dll` | `version.dll` | Alternative entry point |
| `adrena_proxy.ini` | (same) | Configuration |
| `AdrenaProxy Setup.bat` | — | Windows installer |
| `AdrenaProxy Setup.sh` | — | Linux installer |

---

## Compatibility

| Environment | Status |
|---|---|
| Winlator (VKD3D + Turnip Adreno) | ✅ Primary target |
| Wine + DXVK (Linux) | ✅ Supported |
| Windows on Snapdragon (native DX12) | ✅ Supported |
| Windows + NVIDIA/AMD/Intel | ✅ SGSR works universally |

---

## Credits

- **Qualcomm SGSR** — BSD-3-Clause License
- **ImGui** — MIT License
- **OptiScaler** — Architecture inspiration
- **DLSS Enabler** — Pure Extra Present concept

## License

MIT License (AdrenaProxy code) / BSD-3-Clause (SGSR shaders)

## Disclaimer

Not affiliated with Qualcomm Technologies, Inc.  
SGSR™ and Snapdragon™ are trademarks of Qualcomm Technologies, Inc.  
NVIDIA, DLSS, and NVNGX are trademarks of NVIDIA Corporation.
