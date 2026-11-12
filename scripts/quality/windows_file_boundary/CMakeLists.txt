cmake_minimum_required(VERSION 3.20)
project(vpp_windows_file_boundary LANGUAGES C)
if(NOT WIN32 OR NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "Windows x64 is required for the native COFF file-boundary probe")
endif()

find_program(VPP_CLANG_EXECUTABLE NAMES clang clang.exe REQUIRED)
set(ASM_SOURCE "${CMAKE_CURRENT_LIST_DIR}/../../ASM/windows/vpp_aot_primitives_windows_x86_64.S")
set(ASM_OBJECT "${CMAKE_CURRENT_BINARY_DIR}/windows_file_boundary.obj")
add_custom_command(
    OUTPUT "${ASM_OBJECT}"
    COMMAND "${VPP_CLANG_EXECUTABLE}" -target x86_64-pc-windows-msvc
            -DVPP_WINDOWS_BOUNDARY_TEST=1 -DVPP_NATIVE_RUNTIME_STACK_ABI=1
            -c "${ASM_SOURCE}" -o "${ASM_OBJECT}"
    DEPENDS "${ASM_SOURCE}"
    VERBATIM)
set_source_files_properties("${ASM_OBJECT}" PROPERTIES GENERATED TRUE EXTERNAL_OBJECT TRUE)
add_executable(vpp_windows_file_boundary probe.c "${ASM_OBJECT}")
if(MSVC)
    target_compile_options(vpp_windows_file_boundary PRIVATE /utf-8)
endif()
target_link_libraries(vpp_windows_file_boundary PRIVATE kernel32 bcrypt shell32)
enable_testing()
add_test(NAME vpp-windows-file-boundary
         COMMAND vpp_windows_file_boundary "gói biên dịch có dấu 𠮷" "")

# Stage 0 uses C byte/file handles alongside the machine primitives. Link
# this combination independently of the V++ runtime stubs in probe.c.
get_filename_component(VPP_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../.." ABSOLUTE)
set(VPP_STAGE0_SYMBOLS "${CMAKE_CURRENT_BINARY_DIR}/stage0_symbols.c")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${VPP_ROOT}/src/native/aot_native_core.cpp")
include("${CMAKE_CURRENT_LIST_DIR}/stage0_symbols.cmake")
set(STAGE0_OBJECT "${CMAKE_CURRENT_BINARY_DIR}/windows_stage0.obj")
file(GLOB ASM_INCLUDES CONFIGURE_DEPENDS
    "${VPP_ROOT}/scripts/ASM/windows/aot_x86_64/*.inc"
    "${VPP_ROOT}/scripts/ASM/common/*.inc")
add_custom_command(OUTPUT "${STAGE0_OBJECT}"
    COMMAND "${VPP_CLANG_EXECUTABLE}" -target x86_64-pc-windows-msvc
        -DVPP_WINDOWS_STAGE0=1 -DVPP_NATIVE_RUNTIME_STACK_ABI=0
        -c "${ASM_SOURCE}" -o "${STAGE0_OBJECT}"
    DEPENDS "${ASM_SOURCE}" ${ASM_INCLUDES}
    VERBATIM)
set_source_files_properties("${STAGE0_OBJECT}" PROPERTIES GENERATED TRUE EXTERNAL_OBJECT TRUE)
add_executable(vpp_windows_stage0 stage0_probe.c "${VPP_STAGE0_SYMBOLS}"
    "${VPP_ROOT}/src/runtime/native/vpp_aot_runtime.c" "${STAGE0_OBJECT}")
target_compile_features(vpp_windows_stage0 PRIVATE c_std_11)
if(MSVC)
    target_compile_options(vpp_windows_stage0 PRIVATE /utf-8)
endif()
target_link_libraries(vpp_windows_stage0 PRIVATE kernel32 bcrypt shell32)
add_test(NAME vpp-windows-stage0
    COMMAND "${CMAKE_COMMAND}" -E env "VPP_HOME=${VPP_ROOT}"
        $<TARGET_FILE:vpp_windows_stage0> "gói biên dịch có dấu 𠮷" "")
