# Run the native runtime fixture against the V++ runtime and ARM64 ASM.
if (NOT DEFINED VPP_EXEC OR NOT DEFINED VPP_ROOT OR NOT DEFINED VPP_TARGET OR
    NOT DEFINED VPP_OUTPUT_DIR)
    message(FATAL_ERROR "Native runtime: missing test inputs")
endif()
file(MAKE_DIRECTORY "${VPP_OUTPUT_DIR}")
include("${CMAKE_CURRENT_LIST_DIR}/native_stage1_compile.cmake")
set(source "${VPP_ROOT}/src/tests/fixtures/native_runtime.vi")
set(executable "${VPP_OUTPUT_DIR}/native-runtime-check")
file(REMOVE "${executable}" "${VPP_OUTPUT_DIR}/runtime-owned.bin")
set(VPP_HOME "${VPP_ROOT}")
vpp_native_compile_command("${source}" "${executable}" compile_command)
execute_process(
    COMMAND "${CMAKE_COMMAND}" -E env "VPP_HOME=${VPP_ROOT}"
        ${compile_command}
    RESULT_VARIABLE status OUTPUT_VARIABLE output ERROR_VARIABLE errors TIMEOUT 180)
if (NOT status EQUAL 0 OR NOT EXISTS "${executable}")
    message(FATAL_ERROR "V++ native runtime compilation failed: ${status}\n${errors}\n${output}")
endif()
# Guard the V++/ASM AOT path against silently relinking the Stage 0 C++
# driver, VM bridge or legacy native runtime services. Undefined symbols
# alone miss a statically linked fallback that defines its own ABI symbols.
if(APPLE)
    set(runtime_nm /usr/bin/nm)
elseif(UNIX)
    find_program(runtime_nm NAMES nm llvm-nm)
endif()
if(runtime_nm)
    execute_process(COMMAND "${runtime_nm}" -g "${executable}"
        RESULT_VARIABLE nm_status OUTPUT_VARIABLE symbols ERROR_VARIABLE nm_errors TIMEOUT 10)
    set(audited_symbols "${symbols}")
    if(CMAKE_HOST_SYSTEM_NAME STREQUAL "Linux")
        # glibc's C atexit adapter and ELF crt finalization use these two
        # versioned imports even for a plain C executable. Keep every other
        # __cxa_* symbol (and any locally defined one) in the rejection gate.
        string(REGEX REPLACE "[ \t]+[Uw][ \t]+__cxa_(atexit|finalize)@GLIBC_[0-9.]+[ \t]*([\r\n]|$)" "\\2"
            audited_symbols "${audited_symbols}")
    endif()
    if(NOT nm_status STREQUAL "0" OR
        audited_symbols MATCHES "__cxa_|_ZSt|_ZNSt|_Z[0-9]|vpp_aot_|linkedBytecodeRun|executeVpp")
        message(FATAL_ERROR "V++ native AOT linked C++/Stage 0 symbols: ${nm_status}\n${symbols}\n${nm_errors}")
    endif()
else()
    message(FATAL_ERROR "V++ native AOT symbol audit needs nm/llvm-nm")
endif()
if(APPLE)
    execute_process(COMMAND /usr/bin/otool -L "${executable}"
        RESULT_VARIABLE dependency_status OUTPUT_VARIABLE dependencies
        ERROR_VARIABLE dependency_errors TIMEOUT 10)
    if(NOT dependency_status STREQUAL "0" OR
        dependencies MATCHES "libc\\+\\+|libstdc\\+\\+|libffi")
        message(FATAL_ERROR "V++ native AOT linked legacy C++/FFI libraries: ${dependency_status}\n${dependencies}\n${dependency_errors}")
    endif()
elseif(UNIX)
    # Linux also needs a dynamic dependency gate: nm cannot identify a
    # library that is loaded through DT_NEEDED but exposes no used C++ symbol.
    find_program(runtime_readelf NAMES readelf llvm-readelf)
    if(runtime_readelf)
        execute_process(COMMAND "${runtime_readelf}" -d "${executable}"
            RESULT_VARIABLE dependency_status OUTPUT_VARIABLE dependencies
            ERROR_VARIABLE dependency_errors TIMEOUT 10)
        if(NOT dependency_status STREQUAL "0" OR
            dependencies MATCHES "libc\\+\\+|libstdc\\+\\+|libffi")
            message(FATAL_ERROR "V++ native AOT linked legacy C++/FFI libraries: ${dependency_status}\n${dependencies}\n${dependency_errors}")
        endif()
    else()
        message(FATAL_ERROR "V++ native AOT dependency audit requires readelf or llvm-readelf on Linux")
    endif()
endif()
execute_process(COMMAND "${executable}" "" "Tiếng Việt"
    WORKING_DIRECTORY "${VPP_OUTPUT_DIR}"
    RESULT_VARIABLE status OUTPUT_VARIABLE output ERROR_VARIABLE errors TIMEOUT 60)
if (NOT status EQUAL 52 OR NOT output STREQUAL "-2147483648\n2147483647\n0\n")
    message(FATAL_ERROR "V++ native runtime failed: ${status}\n${errors}\n${output}")
endif()
file(READ "${VPP_OUTPUT_DIR}/runtime-owned.bin" actual HEX)
if (NOT actual STREQUAL "00ff0a34")
    message(FATAL_ERROR "V++ native runtime wrote incorrect bytes: ${actual}")
endif()
message(STATUS "V++ native memory/handle/file/argument runtime: PASS")
