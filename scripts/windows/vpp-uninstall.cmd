@echo off
setlocal
rem Parse the whole block before the uninstaller deletes this launcher.
(
    powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0uninstall-vpp.ps1" -InstallDir "%~dp0." %*
    if errorlevel 1 (exit /b 1) else (exit /b 0)
)
