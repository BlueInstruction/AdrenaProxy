# AdrenaProxy environment profiles

These INIs are drop-in starting points for each of the runtime stacks we
validate against.  They don't auto-select — copy the one that matches
your setup to `adrena_proxy.ini` (next to the game executable), or use
the `profile` directive:

```ini
; Top of your adrena_proxy.ini:
profile = winlator
```

When `profile = <name>` is present, AdrenaProxy loads
`profiles/<name>.ini` first (relative to the host DLL), then your local
`adrena_proxy.ini` on top of it — so anything you set locally overrides
the preset.

## Available profiles

| Profile | Target stack | GPU | Notes |
|---|---|---|---|
| [`winlator.ini`](winlator.ini) | Wine/Proton → DXVK / VKD3D-Proton → Turnip on Android | Adreno 6xx / 7xx | Performance preset, x2 FG — the main Winlator target. |
| [`snapdragon_x_elite.ini`](snapdragon_x_elite.ini) | Windows-on-ARM (ARM64EC / Prism) → DX12 | Adreno X1 / X1-85 | Balanced preset, SGSR2 — for thin-and-light ARM laptops. |
| [`steamdeck_proton.ini`](steamdeck_proton.ini) | SteamOS + Proton → DXVK / VKD3D-Proton → RADV | AMD Van Gogh RDNA2 APU | Quality preset, SGSR2, GameScope-friendly FG threshold. |
| [`fex_turnip.ini`](fex_turnip.ini) | FEX-Emu / Box64 → Wine → VKD3D-Proton → Turnip | Adreno on ARM64 Linux | Ultra-Performance, x3 FG — CPU-bound stack, aggressive scaling. |

All four run the same AdrenaProxy DLL — the profile just tunes the
defaults to where each stack's bottleneck actually lives.

## Validation

Each profile is validated by looking at the STATUS card in the overlay:

```
NGX init=N  evaluate=N  last=SGSR1  ok=N
```

If `evaluate > 0` and the card is green, the DLL is hooked correctly on
that stack.  The counters have no dependency on the host repo (Winlator,
Proton, FEX, etc.) — they measure the injected DLL's runtime behaviour.
