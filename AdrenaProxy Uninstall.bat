@echo off
chcp 65001 >nul 2>&1
title AdrenaProxy v2.0 Uninstall

echo.
echo  ╔═══════════════════════════════════════════════════╗
echo  ║       AdrenaProxy v2.0 — Uninstall               ║
echo  ╚═══════════════════════════════════════════════════╝
echo.

echo  Restoring original files...
for %%d in (dxgi.dll dlss.dll nvngx_dlss.dll version.dll) do (
    if exist "%%d.orig" (
        copy /y "%%d.orig" "%%d" >nul 2>&1
        del "%%d.orig" >nul 2>&1
        echo        Restored: %%d
    ) else (
        if exist "%%d" (
            del "%%d" >nul 2>&1
            echo        Removed: %%d
        )
    )
)

if exist "adrena_proxy.log" (
    del "adrena_proxy.log" >nul 2>&1
    echo        Removed: adrena_proxy.log
)

echo.
echo  Config preserved: adrena_proxy.ini (delete manually if needed)
echo.
echo  ╔═══════════════════════════════════════════════════╗
echo  ║          Uninstallation Complete!                 ║
echo  ╚═══════════════════════════════════════════════════╝
echo.
pause