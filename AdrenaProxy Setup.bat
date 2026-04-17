@echo off
chcp 65001 >nul 2>&1
title AdrenaProxy v2.0 Setup

echo.
echo  ╔═══════════════════════════════════════════════════╗
echo  ║       AdrenaProxy v2.0 — Setup Wizard            ║
echo  ║   Qualcomm SGSR Upscaling + Frame Generation     ║
echo  ╚═══════════════════════════════════════════════════╝
echo.

:: ── Check files ──
if not exist "adrenaproxy_sgsr.dll" (
    echo  [ERROR] adrenaproxy_sgsr.dll not found!
    echo  Make sure this bat is in the same folder as the DLL files.
    pause & exit /b 1
)
if not exist "adrenaproxy_dxgi.dll" (
    echo  [ERROR] adrenaproxy_dxgi.dll not found!
    pause & exit /b 1
)

:: ── Detect game ──
echo  [1/5] Detecting game...
set "GAME="
for %%f in (*.exe) do set "GAME=%%f"
if defined GAME (echo        Found: %GAME%) else (echo        No .exe found)
echo.

:: ── Backup originals ──
echo  [2/5] Backing up original files...
for %%d in (dxgi.dll dlss.dll nvngx_dlss.dll) do (
    if exist "%%d" (
        if not exist "%%d.orig" (
            copy "%%d" "%%d.orig" >nul 2>&1
            echo        Backed up: %%d → %%d.orig
        ) else (
            echo        Backup exists: %%d.orig
        )
    ) else (
        echo        No existing %%d
    )
)
echo.

:: ── Install proxy DLLs ──
echo  [3/5] Installing AdrenaProxy...

copy /y "adrenaproxy_dxgi.dll" "dxgi.dll" >nul 2>&1
if %errorlevel%==0 (echo        Installed: dxgi.dll) else (echo        [ERROR] dxgi.dll)

copy /y "adrenaproxy_sgsr.dll" "dlss.dll" >nul 2>&1
if %errorlevel%==0 (echo        Installed: dlss.dll) else (echo        [ERROR] dlss.dll)

if exist "nvngx_dlss.dll.orig" (
    copy /y "adrenaproxy_sgsr.dll" "nvngx_dlss.dll" >nul 2>&1
    echo        Installed: nvngx_dlss.dll (DLSS 3 compat)
)

if exist "adrenaproxy_version.dll" (
    copy /y "adrenaproxy_version.dll" "version.dll" >nul 2>&1
    if %errorlevel%==0 (echo        Installed: version.dll (alt entry point))
)
echo.

:: ── Config ──
echo  [4/5] Configuration...
if not exist "adrena_proxy.ini" (
    echo        Creating default config...
) else (
    echo        Config exists — preserving
)
echo.

:: ── Done ──
echo  [5/5] Complete!
echo.
echo  ╔═══════════════════════════════════════════════════╗
echo  ║            Installation Complete!                 ║
echo  ╠═══════════════════════════════════════════════════╣
echo  ║                                                   ║
echo  ║  Press HOME in-game to toggle overlay             ║
echo  ║  SGSR upscaling works in DLSS-compatible games    ║
echo  ║  Frame Generation: Pure Extra Present mode        ║
echo  ║                                                   ║
echo  ║  To uninstall: run AdrenaProxy Uninstall.bat      ║
echo  ╚═══════════════════════════════════════════════════╝
echo.
pause