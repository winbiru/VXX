@echo off
setlocal DisableDelayedExpansion
rem Match the UTF-8 Vietnamese command names, as in the VDK launcher.
chcp 65001 >nul 2>nul
for %%I in ("%~dp0..\share\vpp") do set "VPP_HOME=%%~fI"
set "VPP_HOME=%VPP_HOME:\=/%"
set "VPP_AOT_VPP_OBJECT=%VPP_HOME%/native-vpp-runtime/runtime.o"
set "VPP_AOT_CORE_OBJECT=%VPP_HOME%/native-vpp-runtime/runtime-core.o"
set "VPP_AOT_PLATFORM_OBJECT=%VPP_HOME%/native-vpp-runtime/platform.o"
set "VPP_WINDOWS_RUNTIME_OBJECT=%VPP_HOME%/windows-runtime.obj"
set "VPP_NATIVE_TARGET=windows-x86_64"
if not defined VPP_AOT_ASSEMBLER set "VPP_AOT_ASSEMBLER=clang.exe"
if /I "%~1"=="uninstall" if "%~2"=="" goto uninstall
if /I "%~1"=="gỡ cài đặt" if "%~2"=="" goto uninstall
if /I "%~1"=="gỡ-cài-đặt" if "%~2"=="" goto uninstall
if /I "%~1"=="gỡ" if /I "%~2"=="cài" if /I "%~3"=="đặt" if "%~4"=="" goto uninstall
if /I "%~1"=="gỡ" if /I "%~2"=="cài đặt" if "%~3"=="" goto uninstall
if "%~1"=="jit" goto jit
if /I "%~1"=="build" goto build
if /I "%~1"=="format" if exist "%~2\NUL" goto tools
if /I "%~1"=="lint" if exist "%~2\NUL" goto tools
if /I "%~1"=="batch-build" goto tools
if /I "%~1"=="vm-build" goto tools
if /I "%~1"=="artifact" goto tools
if /I "%~1"=="embed" goto native
if /I "%~1"=="disassemble" goto native
if /I "%~1"=="dump-ast" goto native
if /I "%~1"=="dump-ir" goto native
if /I "%~1"=="format" goto native
if /I "%~1"=="lint" goto native
if /I "%~1"=="emit-program-asm" goto tools
:tools
if not exist "%~dp0vpp-tools.exe" goto native
"%~dp0vpp-tools.exe" "%~dp0vpp-native.exe" %*
exit /b %errorlevel%
:jit
rem vpp-jit accepts the leading jit argument; keep all original argv and stdio.
set "jit_source=%~2"
if "%~2"=="--dynamic" set "jit_source=%~3"
if "%~2"=="--check-dynamic" set "jit_source=%~3"
if "%~2"=="--raw-program" set "jit_source=%~3"
if "%~2"=="--vm-image" set "jit_source=%~3"
if not exist "%~dp0vpp-tools.exe" goto jit_run
if not defined jit_source goto jit_run
if not exist "%jit_source%" goto jit_run
"%~dp0vpp-tools.exe" "%~dp0vpp-native.exe" --check-source-lock "%jit_source%"
if errorlevel 1 exit /b %errorlevel%
:jit_run
"%~dp0vpp-jit.exe" %*
exit /b %errorlevel%
:build
if not exist "%~dp0vpp-tools.exe" goto native
if not "%~3"=="-o" goto tools
if "%~4"=="" goto tools
if not "%~5"=="" goto tools
if not exist "%~2" goto tools
if exist "%~2" "%~dp0vpp-tools.exe" "%~dp0vpp-native.exe" --kiểm-khóa-nguồn "%~2"
if errorlevel 1 exit /b %errorlevel%
:native
"%~dp0vpp-native.exe" %*
exit /b %errorlevel%

:uninstall
if not exist "%~dp0uninstall-native.ps1" exit /b 2
rem Leave the batch context before PowerShell removes this file. Keep both
rem commands on one line so cmd never reopens the deleted launcher.
(goto) 2>nul & "%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe" -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%~dp0uninstall-native.ps1" -Root "%~dp0.."
