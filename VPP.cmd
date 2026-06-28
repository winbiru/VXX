@echo off
setlocal

set ROOT=%~dp0

if exist "%ROOT%cmake-build-debug\bin\vpp-cli.exe" (
  "%ROOT%cmake-build-debug\bin\vpp-cli.exe" %*
  exit /b %ERRORLEVEL%
)

if exist "%ROOT%build\bin\vietvm-cli.exe" (
  "%ROOT%build\bin\vietvm-cli.exe" %*
  exit /b %ERRORLEVEL%
)

echo VPP: khong tim thay binary CLI. Hay build truoc.
echo   cmake -S . -B cmake-build-debug
echo   cmake --build cmake-build-debug --config Debug
exit /b 1