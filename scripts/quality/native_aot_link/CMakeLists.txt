cmake_minimum_required(VERSION 3.20)
project(vpp_native_aot_link LANGUAGES CXX)
foreach(required IN ITEMS VPP_AOT_ROOT VPP_AOT_SOURCE VPP_AOT_OUTPUT)
    if (NOT DEFINED ${required})
        message(FATAL_ERROR "Missing ${required}")
    endif()
endforeach()
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
        "-I${VPP_AOT_ROOT}/scripts/ASM" -o "${object}"
    DEPENDS "${VPP_AOT_SOURCE}"
    VERBATIM)
set_source_files_properties("${object}" PROPERTIES GENERATED TRUE EXTERNAL_OBJECT TRUE)
add_executable(vpp-aot-program "${object}")
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
