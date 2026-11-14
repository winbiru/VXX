@echo off
setlocal
for %%I in ("%~dp0..\share\vpp") do set "VPP_HOME=%%~fI"
set "VPP_HOME=%VPP_HOME:\=/%"
set "VPP_AOT_VPP_OBJECT=%VPP_HOME%/native-vpp-runtime/runtime.o"
set "VPP_AOT_CORE_OBJECT=%VPP_HOME%/native-vpp-runtime/runtime-core.o"
set "VPP_AOT_PLATFORM_OBJECT=%VPP_HOME%/native-vpp-runtime/platform.o"
set "VPP_WINDOWS_RUNTIME_OBJECT=%VPP_HOME%/windows-runtime.obj"
set "VPP_NATIVE_TARGET=windows-x86_64"
if not defined VPP_AOT_ASSEMBLER set "VPP_AOT_ASSEMBLER=clang.exe"
"%~dp0vpp-native.exe" %*
exit /b %errorlevel%
