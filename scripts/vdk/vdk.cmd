@echo off
setlocal DisableDelayedExpansion
rem V++ writes UTF-8 bytes. Keep the console in UTF-8 through child execution.
chcp 65001 >nul 2>nul
set "VDK_SELF=%~dp0"
set "VDK_TEMP=%TEMP%\vdk-%RANDOM%-%RANDOM%.txt"
"%VDK_SELF%vdk-info.exe" --select-sdk "%VDK_SELF%.." %* > "%VDK_TEMP%"
if errorlevel 1 goto failed
set "VDK_HOME="
for /f "usebackq delims=" %%I in ("%VDK_TEMP%") do set "VDK_HOME=%%I"
if not defined VDK_HOME goto failed
set "VDK_HOME=%VDK_HOME:\=/%"
"%VDK_SELF%vdk-info.exe" --route %* > "%VDK_TEMP%"
if errorlevel 1 goto failed
set "VDK_KIND="
for /f "usebackq tokens=1,2,3" %%I in ("%VDK_TEMP%") do (
    set "VDK_KIND=%%I"
    set "VDK_COMMAND=%%J"
)
if "%VDK_KIND%"=="info" goto info
if "%VDK_KIND%"=="display" goto display
if "%VDK_KIND%"=="uninstall" goto uninstall
set "VPP_HOME=%VDK_HOME%/share/vpp"
set "VPP_AOT_VPP_OBJECT=%VPP_HOME%/native-vpp-runtime/runtime.o"
set "VPP_AOT_CORE_OBJECT=%VPP_HOME%/native-vpp-runtime/runtime-core.o"
set "VPP_AOT_PLATFORM_OBJECT=%VPP_HOME%/native-vpp-runtime/platform.o"
set "VPP_WINDOWS_RUNTIME_OBJECT=%VPP_HOME%/windows-runtime.obj"
set "VPP_NATIVE_TARGET=windows-x86_64"
if not defined VPP_AOT_ASSEMBLER set "VPP_AOT_ASSEMBLER=clang.exe"
if "%VDK_KIND%"=="run" goto run
"%VDK_SELF%vdk-info.exe" --windows-tail %* > "%VDK_TEMP%"
if errorlevel 1 goto failed
set "VDK_TAIL="
for /f "usebackq delims=" %%I in ("%VDK_TEMP%") do set "VDK_TAIL=%%I"
del "%VDK_TEMP%"
setlocal EnableDelayedExpansion
if "!VDK_KIND!"=="jit" goto jit
"!VDK_HOME!/bin/vpp-tools.exe" "!VDK_HOME!/bin/vpp-native.exe" !VDK_TAIL!
exit /b %errorlevel%
:jit
"!VDK_HOME!/bin/vpp-tools.exe" "!VDK_HOME!/bin/vpp-native.exe" jit !VDK_TAIL!
exit /b %errorlevel%
:run
"%VDK_SELF%vdk-info.exe" --windows-source %* > "%VDK_TEMP%"
if errorlevel 1 goto failed
set "VDK_SOURCE="
for /f "usebackq delims=" %%I in ("%VDK_TEMP%") do set "VDK_SOURCE=%%I"
"%VDK_SELF%vdk-info.exe" --windows-program-tail %* > "%VDK_TEMP%"
if errorlevel 1 goto failed
set "VDK_TAIL="
for /f "usebackq delims=" %%I in ("%VDK_TEMP%") do set "VDK_TAIL=%%I"
del "%VDK_TEMP%"
set "VDK_RUN=%TEMP%\vdk-run-%RANDOM%-%RANDOM%"
mkdir "%VDK_RUN%" || exit /b 2
setlocal EnableDelayedExpansion
"!VDK_HOME!/bin/vpp-tools.exe" "!VDK_HOME!/bin/vpp-native.exe" --kiểm-khóa-nguồn !VDK_SOURCE!
if errorlevel 1 goto run_done
"!VDK_HOME!/bin/vpp-native.exe" build !VDK_SOURCE! -o "!VDK_RUN!/program.exe"
if errorlevel 1 goto run_done
"!VDK_RUN!/program.exe" !VDK_TAIL!
:run_done
set "VDK_RESULT=%errorlevel%"
rmdir /s /q "!VDK_RUN!"
exit /b %VDK_RESULT%
:info
del "%VDK_TEMP%"
"%VDK_SELF%vdk-info.exe" %*
exit /b %errorlevel%
:display
del "%VDK_TEMP%"
"%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe" -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%VDK_HOME%/bin/vdk-display.ps1" -Mode "%VDK_COMMAND%"
exit /b %errorlevel%
:uninstall
if not exist "%VDK_HOME%/bin/uninstall-native.ps1" goto failed
del "%VDK_TEMP%"
rem Expand the SDK path before leaving the batch context, then run the
rem uninstaller synchronously in the caller's context.
(goto) 2>nul & "%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe" -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%VDK_HOME%/bin/uninstall-native.ps1" -Root "%VDK_HOME%"
:failed
type "%VDK_TEMP%" 1>&2
del "%VDK_TEMP%"
exit /b 2
