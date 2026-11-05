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
