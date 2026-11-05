cmake_minimum_required(VERSION 3.20)
set(vpp_aot_runtime "$ENV{VPP_AOT_RUNTIME}")
if (vpp_aot_runtime STREQUAL "vpp" OR vpp_aot_runtime STREQUAL "vpp-freestanding")
    # These Stage 0 configuration values only apply to the legacy archives.
    set(vpp_aot_legacy_configuration
        "${VPP_AOT_CONFIGURATION};${VPP_AOT_LIB_ROOT};${VPP_AOT_LINK_LEGACY_LIBFFI}")
    project(vpp_native_aot_link LANGUAGES NONE)
elseif (vpp_aot_runtime STREQUAL "" OR vpp_aot_runtime STREQUAL "cpp")
    project(vpp_native_aot_link LANGUAGES CXX)
else()
    message(FATAL_ERROR "VPP_AOT_RUNTIME must be vpp, vpp-freestanding or cpp")
endif()
foreach(required IN ITEMS VPP_AOT_ROOT VPP_AOT_SOURCE VPP_AOT_OUTPUT)
    if (NOT DEFINED ${required})
        message(FATAL_ERROR "Missing ${required}")
    endif()
endforeach()
# Already bootstrapped native compilers may still emit the former flat
# include name. Search common/ in addition to ASM/ while those seeds are
# upgraded; all actual runtime sources stay in platform/common directories.
set(vpp_aot_asm_include_dirs
    "-I${VPP_AOT_ROOT}/scripts/ASM"
    "-I${VPP_AOT_ROOT}/scripts/ASM/common")
# Linux x86-64: compile V++-emitted ASM and its V++-authored runtime into a
# static ELF. All host services enter through the raw syscall assembly layer.
# Keep this route independent from the Darwin and Stage 0 C++ linkers.
if (vpp_aot_runtime STREQUAL "vpp-freestanding")
    if (NOT CMAKE_SYSTEM_NAME STREQUAL "Linux" OR
        NOT CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|amd64|AMD64)$")
        message(FATAL_ERROR "V++ freestanding AOT link requires Linux x86-64")
    endif()
    set(runtime_object "$ENV{VPP_AOT_VPP_OBJECT}")
    if (runtime_object STREQUAL "")
        set(runtime_object "${VPP_AOT_ROOT}/build/native-vpp-runtime/runtime.o")
    endif()
    if (NOT IS_ABSOLUTE "${runtime_object}" OR NOT EXISTS "${runtime_object}")
        message(FATAL_ERROR "Produce a Linux ELF runtime.o with scripts/build-vpp-aot-runtime.sh and set VPP_AOT_VPP_OBJECT to its absolute path")
    endif()
    find_program(VPP_AOT_ASSEMBLER NAMES clang REQUIRED)
    set(asm_dir "${VPP_AOT_ROOT}/scripts/ASM")
    file(GLOB_RECURSE asm_includes CONFIGURE_DEPENDS
        "${asm_dir}/*.inc" "${asm_dir}/*.S" "${asm_dir}/*.s")
    set(objects)
    foreach(component IN ITEMS program start syscalls runtime-core directory sha256 process socket bridge numeric)
        if (component STREQUAL "program")
            set(source "${VPP_AOT_SOURCE}")
            set(flags -DVPP_AOT_VPP_RUNTIME=1)
        elseif (component STREQUAL "start")
            set(source "${asm_dir}/linux/vpp_linux_start_x86_64.S")
            set(flags)
        elseif (component STREQUAL "syscalls")
            set(source "${asm_dir}/linux/vpp_linux_syscalls_x86_64.S")
            set(flags)
        elseif (component STREQUAL "runtime-core")
            set(source "${asm_dir}/linux/vpp_linux_runtime_core_x86_64.S")
            set(flags)
        elseif (component STREQUAL "directory")
            set(source "${asm_dir}/linux/vpp_linux_directory_x86_64.S")
            set(flags)
        elseif (component STREQUAL "sha256")
            set(source "${asm_dir}/linux/vpp_linux_sha256_x86_64.S")
            set(flags)
        elseif (component STREQUAL "process")
            set(source "${asm_dir}/linux/vpp_linux_process_x86_64.S")
            set(flags)
        elseif (component STREQUAL "socket")
            set(source "${asm_dir}/linux/vpp_linux_socket_x86_64.S")
            set(flags)
        elseif (component STREQUAL "bridge")
            set(source "${asm_dir}/linux/vpp_linux_aot_runtime_bridge_x86_64.S")
            set(flags)
        else()
            set(source "${asm_dir}/linux/vpp_linux_f64_x86_64.S")
            set(flags)
        endif()
        set(object "${CMAKE_CURRENT_BINARY_DIR}/${component}.o")
        add_custom_command(OUTPUT "${object}"
            COMMAND "${VPP_AOT_ASSEMBLER}" -target x86_64-unknown-linux-gnu
                -c "${source}" ${flags} ${vpp_aot_asm_include_dirs} -o "${object}"
            DEPENDS "${source}" ${asm_includes}
            VERBATIM)
        list(APPEND objects "${object}")
    endforeach()
    set(executable "${CMAKE_CURRENT_BINARY_DIR}/bin/vpp-aot-program")
    add_custom_command(OUTPUT "${executable}"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/bin"
        COMMAND "${VPP_AOT_ASSEMBLER}" -target x86_64-unknown-linux-gnu
            -nostdlib -static -no-pie -Wl,-e,_start
            -Wl,--no-undefined -Wl,--build-id=none -Wl,-z,noexecstack
            ${objects} "${runtime_object}" -o "${executable}"
        COMMAND /bin/bash "${VPP_AOT_ROOT}/scripts/quality/audit_linux_static_elf.sh"
            "${executable}"
        DEPENDS ${objects} "${runtime_object}"
            "${VPP_AOT_ROOT}/scripts/quality/audit_linux_static_elf.sh"
        VERBATIM)
    add_custom_target(vpp-aot-program ALL DEPENDS "${executable}")
    add_custom_command(TARGET vpp-aot-program POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E copy "${executable}" "${VPP_AOT_OUTPUT}"
        VERBATIM)
    return()
endif()
# The dynamic V++ runtime is a native object, built by the self-hosted i32
# compiler. No Stage 0 archives or foreign-call library enter this link.
if (vpp_aot_runtime STREQUAL "vpp")
    if (NOT APPLE OR NOT CMAKE_SYSTEM_PROCESSOR MATCHES "^(arm64|aarch64)$")
        message(FATAL_ERROR "V++ dynamic AOT runtime currently requires macOS ARM64")
    endif()
    set(runtime_object "$ENV{VPP_AOT_VPP_OBJECT}")
    if (runtime_object STREQUAL "")
        set(runtime_object "${VPP_AOT_ROOT}/build/native-vpp-runtime/runtime.o")
    endif()
    if (NOT IS_ABSOLUTE "${runtime_object}" OR NOT EXISTS "${runtime_object}")
        message(FATAL_ERROR "Build the V++ runtime with scripts/build-vpp-aot-runtime.sh, then set VPP_AOT_VPP_OBJECT to its absolute runtime.o path")
    endif()
    set(program_object "${CMAKE_CURRENT_BINARY_DIR}/program.o")
    set(primitives_object "${CMAKE_CURRENT_BINARY_DIR}/primitives.o")
    find_program(VPP_AOT_ASSEMBLER NAMES clang REQUIRED)
    add_custom_command(OUTPUT "${program_object}"
        COMMAND "${VPP_AOT_ASSEMBLER}" -c "${VPP_AOT_SOURCE}"
            -DVPP_AOT_VPP_RUNTIME=1 ${vpp_aot_asm_include_dirs}
            -o "${program_object}"
        DEPENDS "${VPP_AOT_SOURCE}"
            "${VPP_AOT_ROOT}/scripts/ASM/common/vpp_aot_dynamic_runtime.inc"
            "${VPP_AOT_ROOT}/scripts/ASM/common/vpp_aot_vpp_runtime.inc"
        VERBATIM)
    file(GLOB_RECURSE primitive_dependencies CONFIGURE_DEPENDS
        "${VPP_AOT_ROOT}/scripts/ASM/*.inc"
        "${VPP_AOT_ROOT}/scripts/ASM/*.S")
    add_custom_command(OUTPUT "${primitives_object}"
        COMMAND "${VPP_AOT_ASSEMBLER}" -c
            "${VPP_AOT_ROOT}/scripts/ASM/macos/vpp_aot_primitives_aarch64.S"
            -DVPP_NATIVE_RUNTIME_STACK_ABI=1 -o "${primitives_object}"
        DEPENDS "${VPP_AOT_ROOT}/scripts/ASM/macos/vpp_aot_primitives_aarch64.S"
            ${primitive_dependencies}
        VERBATIM)
    # Use only the assembler/linker driver. Enabling C/CXX here would also
    # compile CMake's generated compiler-identification sources at configure.
    set(executable "${CMAKE_CURRENT_BINARY_DIR}/bin/vpp-aot-program")
    add_custom_command(OUTPUT "${executable}"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/bin"
        COMMAND "${VPP_AOT_ASSEMBLER}" "${program_object}" "${primitives_object}"
            "${runtime_object}" -Wl,-dead_strip -o "${executable}"
        DEPENDS "${program_object}" "${primitives_object}" "${runtime_object}"
        VERBATIM)
    add_custom_target(vpp-aot-program ALL DEPENDS "${executable}")
    add_custom_command(TARGET vpp-aot-program POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E copy "${executable}" "${VPP_AOT_OUTPUT}"
        VERBATIM)
    return()
endif()
set(CMAKE_CXX_STANDARD 17)
if (MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY MultiThreaded)
    find_program(VPP_AOT_ASSEMBLER NAMES clang REQUIRED)
    set(object "${CMAKE_CURRENT_BINARY_DIR}/program.obj")
else()
    set(VPP_AOT_ASSEMBLER "${CMAKE_CXX_COMPILER}")
    set(object "${CMAKE_CURRENT_BINARY_DIR}/program.o")
endif()
add_custom_command(OUTPUT "${object}"
    COMMAND "${VPP_AOT_ASSEMBLER}" -c "${VPP_AOT_SOURCE}"
        ${vpp_aot_asm_include_dirs} -o "${object}"
    DEPENDS "${VPP_AOT_SOURCE}"
    VERBATIM)
set_source_files_properties("${object}" PROPERTIES GENERATED TRUE EXTERNAL_OBJECT TRUE)
set(vpp_aot_bootstrap_objects)
if (NOT MSVC AND (APPLE OR CMAKE_SYSTEM_NAME STREQUAL "Linux"))
    find_program(VPP_AOT_C_COMPILER NAMES clang cc REQUIRED)
    set(bootstrap_runtime_object "${CMAKE_CURRENT_BINARY_DIR}/bootstrap-runtime.o")
    add_custom_command(OUTPUT "${bootstrap_runtime_object}"
        COMMAND "${VPP_AOT_C_COMPILER}" -std=c11 -c
            "${VPP_AOT_ROOT}/src/runtime/native/vpp_aot_runtime.c"
            -o "${bootstrap_runtime_object}"
        DEPENDS "${VPP_AOT_ROOT}/src/runtime/native/vpp_aot_runtime.c"
        VERBATIM)
    list(APPEND vpp_aot_bootstrap_objects "${bootstrap_runtime_object}")
endif()
if (APPLE AND CMAKE_SYSTEM_PROCESSOR MATCHES "^(arm64|aarch64)$")
    set(stage0_machine_object "${CMAKE_CURRENT_BINARY_DIR}/stage0-machine.o")
    file(GLOB stage0_machine_includes CONFIGURE_DEPENDS
        "${VPP_AOT_ROOT}/scripts/ASM/macos/aot_arm64/*.inc")
    add_custom_command(OUTPUT "${stage0_machine_object}"
        COMMAND "${VPP_AOT_ASSEMBLER}" -c
            "${VPP_AOT_ROOT}/scripts/ASM/macos/vpp_aot_stage0_machine_aarch64.S"
            ${vpp_aot_asm_include_dirs} -o "${stage0_machine_object}"
        DEPENDS "${VPP_AOT_ROOT}/scripts/ASM/macos/vpp_aot_stage0_machine_aarch64.S"
            ${stage0_machine_includes}
        VERBATIM)
    list(APPEND vpp_aot_bootstrap_objects "${stage0_machine_object}")
endif()
set_source_files_properties(${vpp_aot_bootstrap_objects}
    PROPERTIES GENERATED TRUE EXTERNAL_OBJECT TRUE)
add_executable(vpp-aot-program "${object}" ${vpp_aot_bootstrap_objects})
set_target_properties(vpp-aot-program PROPERTIES LINKER_LANGUAGE CXX
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/bin")
if (NOT DEFINED VPP_AOT_LIB_ROOT)
    set(VPP_AOT_LIB_ROOT "${VPP_AOT_ROOT}")
endif()
# Single-config generators place archives directly under lib/, whereas
# Visual Studio puts them under lib/<configuration>/. Keep the link configuration
# aligned with the --config used by the driver (Release for Stage 0).
if (NOT DEFINED VPP_AOT_CONFIGURATION)
    if (CMAKE_BUILD_TYPE)
        set(VPP_AOT_CONFIGURATION "${CMAKE_BUILD_TYPE}")
    else()
        set(VPP_AOT_CONFIGURATION "Release")
    endif()
endif()
set(VPP_AOT_LIBRARY_DIRS
    "${VPP_AOT_LIB_ROOT}/lib/${VPP_AOT_CONFIGURATION}"
    "${VPP_AOT_LIB_ROOT}/lib64/${VPP_AOT_CONFIGURATION}"
    "${VPP_AOT_LIB_ROOT}/lib"
    "${VPP_AOT_LIB_ROOT}/lib64")
foreach(module IN ITEMS native-aot runtime-native compiler frontend bytecode core)
    find_library(vpp_${module} NAMES vpp-${module}
        PATHS ${VPP_AOT_LIBRARY_DIRS}
        NO_DEFAULT_PATH REQUIRED)
    target_link_libraries(vpp-aot-program PRIVATE "${vpp_${module}}")
endforeach()
# Native Stage 0 bridges also run inside the coverage build (for example,
# target parser parity). Its static archives contain gcov/LLVM coverage
# references, so the generated executable must link the matching runtime.
# Read the cache belonging to the selected archives instead of assuming that
# the nested linker's Release configuration describes their instrumentation.
if (NOT MSVC AND EXISTS "${VPP_AOT_LIB_ROOT}/CMakeCache.txt")
    set(vpp_aot_cache "${VPP_AOT_LIB_ROOT}/CMakeCache.txt")
    file(STRINGS "${vpp_aot_cache}" vpp_aot_coverage
        REGEX "^VPP_ENABLE_COVERAGE:BOOL=ON$")
    if (vpp_aot_coverage)
        target_link_options(vpp-aot-program PRIVATE --coverage)
    endif()
    # The sanitized CI build supplies -fsanitize through the parent CMake
    # cache. Generated executables linking those instrumented archives must
    # load the same sanitizer runtime even though this helper builds in Release.
    file(STRINGS "${vpp_aot_cache}" vpp_aot_flag_entries
        REGEX "^(CMAKE_CXX_FLAGS|CMAKE_EXE_LINKER_FLAGS):STRING=")
    set(vpp_aot_sanitizers)
    foreach(entry IN LISTS vpp_aot_flag_entries)
        string(REGEX REPLACE "^CMAKE_(CXX_FLAGS|EXE_LINKER_FLAGS):STRING=" "" flags "${entry}")
        separate_arguments(flags NATIVE_COMMAND "${flags}")
        foreach(flag IN LISTS flags)
            if (flag MATCHES "^-fsanitize=")
                list(APPEND vpp_aot_sanitizers "${flag}")
            endif()
        endforeach()
    endforeach()
    if (vpp_aot_sanitizers)
        list(REMOVE_DUPLICATES vpp_aot_sanitizers)
        target_link_options(vpp-aot-program PRIVATE ${vpp_aot_sanitizers})
    endif()
endif()
find_package(Threads REQUIRED)
target_link_libraries(vpp-aot-program PRIVATE Threads::Threads ${CMAKE_DL_LIBS})
if (WIN32)
    target_link_libraries(vpp-aot-program PRIVATE ws2_32 bcrypt secur32 crypt32)
else()
    # Link the same ABI as the runtime archives selected by the AOT driver.
    # Machine-only executables must not gain a libffi dependency here.
    if (NOT DEFINED VPP_AOT_LINK_LEGACY_LIBFFI)
        message(FATAL_ERROR "AOT linker requires VPP_AOT_LINK_LEGACY_LIBFFI")
    endif()
    if (VPP_AOT_LINK_LEGACY_LIBFFI)
        find_library(VPP_AOT_FFI NAMES ffi REQUIRED)
        target_link_libraries(vpp-aot-program PRIVATE "${VPP_AOT_FFI}")
    endif()
    if (APPLE)
        find_library(VPP_AOT_SECURITY Security REQUIRED)
        find_library(VPP_AOT_CORE_FOUNDATION CoreFoundation REQUIRED)
        target_link_libraries(vpp-aot-program PRIVATE "${VPP_AOT_SECURITY}" "${VPP_AOT_CORE_FOUNDATION}")
    else()
        find_package(OpenSSL REQUIRED)
        target_link_libraries(vpp-aot-program PRIVATE OpenSSL::SSL OpenSSL::Crypto)
    endif()
endif()
add_custom_command(TARGET vpp-aot-program POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E copy "$<TARGET_FILE:vpp-aot-program>" "${VPP_AOT_OUTPUT}"
    VERBATIM)
