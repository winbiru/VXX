@echo off
setlocal

for %%I in ("%~dp0..") do set "ROOT=%%~fI"
set "BUILD_DIR=%ROOT%\cmake-build-debug"

if not exist "%BUILD_DIR%" mkdir "%BUILD_DIR%"

cmake -S "%ROOT%" -B "%BUILD_DIR%"
if errorlevel 1 exit /b %errorlevel%

cmake --build "%BUILD_DIR%" --config Debug
if errorlevel 1 exit /b %errorlevel%

echo Build xong: "%BUILD_DIR%\bin\vpp-cli.exe"
exit /b 0
