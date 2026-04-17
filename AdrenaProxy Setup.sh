#!/bin/bash
# AdrenaProxy v2.0 Setup — Linux/Wine/Proton/Winlator

set -e

echo ""
echo " ╔═══════════════════════════════════════════════════╗"
echo " ║       AdrenaProxy v2.0 — Setup (Linux)           ║"
echo " ║   Qualcomm SGSR Upscaling + Frame Generation     ║"
echo " ╚═══════════════════════════════════════════════════╝"
echo ""

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$SCRIPT_DIR"

# Check files
if [ ! -f "adrenaproxy_sgsr.dll" ] || [ ! -f "adrenaproxy_dxgi.dll" ]; then
    echo " [ERROR] DLL files not found in $SCRIPT_DIR"
    exit 1
fi

# Find game directory (first argument or current directory)
GAME_DIR="${1:-.}"
cd "$GAME_DIR"
echo " [1/5] Game directory: $(pwd)"

# Backup originals
echo " [2/5] Backing up..."
for f in dxgi.dll dlss.dll nvngx_dlss.dll version.dll; do
    if [ -f "$f" ] && [ ! -f "${f}.orig" ]; then
        cp "$f" "${f}.orig"
        echo "       Backed up: $f"
    fi
done

# Install
echo " [3/5] Installing..."
cp "${SCRIPT_DIR}/adrenaproxy_dxgi.dll" "dxgi.dll"
echo "       Installed: dxgi.dll"

cp "${SCRIPT_DIR}/adrenaproxy_sgsr.dll" "dlss.dll"
echo "       Installed: dlss.dll"

if [ -f "nvngx_dlss.dll.orig" ]; then
    cp "${SCRIPT_DIR}/adrenaproxy_sgsr.dll" "nvngx_dlss.dll"
    echo "       Installed: nvngx_dlss.dll"
fi

if [ -f "${SCRIPT_DIR}/adrenaproxy_version.dll" ]; then
    cp "${SCRIPT_DIR}/adrenaproxy_version.dll" "version.dll"
    echo "       Installed: version.dll"
fi

# Config
echo " [4/5] Configuration..."
if [ ! -f "adrena_proxy.ini" ] && [ -f "${SCRIPT_DIR}/adrena_proxy.ini" ]; then
    cp "${SCRIPT_DIR}/adrena_proxy.ini" "adrena_proxy.ini"
    echo "       Created: adrena_proxy.ini"
fi

echo " [5/5] Complete!"
echo ""
echo " Press HOME in-game to toggle overlay"
echo " To uninstall: delete dlss.dll, dxgi.dll and rename .orig files"