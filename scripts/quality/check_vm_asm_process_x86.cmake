# Exercise each runnable POSIX process ASM ABI. The C harness supplies only
# an arena and assertions; production process policy remains in V++/ASM.
set(host_processor "${CMAKE_HOST_SYSTEM_PROCESSOR}")
if(NOT host_processor)
    execute_process(COMMAND uname -m OUTPUT_VARIABLE host_processor
        OUTPUT_STRIP_TRAILING_WHITESPACE)
endif()
if(CMAKE_HOST_SYSTEM_NAME STREQUAL "Darwin")
    if(NOT host_processor MATCHES "^(x86_64|arm64)$")
        message(STATUS "V++ process runtime: unsupported Darwin host, skipped")
        return()
    endif()
    set(process_arches x86_64)
    if(host_processor STREQUAL "arm64")
        set(process_arches arm64 x86_64)
    endif()
elseif(CMAKE_HOST_SYSTEM_NAME STREQUAL "Linux" AND
       host_processor MATCHES "^(x86_64|AMD64)$")
    set(process_arches x86_64)
elseif(CMAKE_HOST_SYSTEM_NAME STREQUAL "Linux" AND
       host_processor MATCHES "^(aarch64|arm64)$")
    set(process_arches arm64)
else()
    message(STATUS "V++ process runtime: unsupported host, skipped")
    return()
endif()
find_program(VPP_PROCESS_CLANG NAMES clang REQUIRED)
foreach(process_arch IN LISTS process_arches)
    set(target_flags "")
    set(run_prefix "")
    if(CMAKE_HOST_SYSTEM_NAME STREQUAL "Darwin")
        set(target_flags -arch "${process_arch}")
        set(run_prefix /usr/bin/arch -${process_arch})
        if(process_arch STREQUAL "x86_64" AND host_processor STREQUAL "arm64")
            execute_process(COMMAND /usr/bin/arch -x86_64 /usr/bin/true
                RESULT_VARIABLE rosetta_status ERROR_QUIET)
            if(NOT rosetta_status STREQUAL "0")
                message(STATUS "V++ process x86_64: Rosetta unavailable, skipping runtime")
                continue()
            endif()
        endif()
    endif()
    set(process_dir "${VPP_OUTPUT_DIR}/process-${process_arch}")
    file(MAKE_DIRECTORY "${process_dir}")
    execute_process(COMMAND "${VPP_PROCESS_CLANG}" ${target_flags}
        -x assembler-with-cpp -c "${VPP_ROOT}/scripts/ASM/vpp_vm_process_primitives.inc"
        -o "${process_dir}/process.o"
        RESULT_VARIABLE status ERROR_VARIABLE errors TIMEOUT 30)
    if(NOT status STREQUAL "0")
        message(FATAL_ERROR "V++ ${process_arch} process ASM compile failed: ${status}\n${errors}")
    endif()
    execute_process(COMMAND "${VPP_PROCESS_CLANG}" ${target_flags} -Wall -Wextra -Werror
        "${VPP_ROOT}/src/tests/fixtures/vm_asm_process_x86_harness.c"
        "${process_dir}/process.o" -o "${process_dir}/process-harness"
        RESULT_VARIABLE status ERROR_VARIABLE errors TIMEOUT 30)
    if(NOT status STREQUAL "0")
        message(FATAL_ERROR "V++ ${process_arch} process harness link failed: ${status}\n${errors}")
    endif()
    execute_process(COMMAND ${run_prefix} "${process_dir}/process-harness"
        RESULT_VARIABLE status OUTPUT_VARIABLE output ERROR_VARIABLE errors TIMEOUT 60)
    if(NOT status STREQUAL "0" OR NOT output STREQUAL "VM ASM PROCESS OK\n")
        message(FATAL_ERROR "V++ ${process_arch} process runtime mismatch: ${status}\n${output}\n${errors}")
    endif()
    message(STATUS "V++/ASM process ${process_arch}: spawn/pipes/exit/signal/env/fd ownership PASS")
endforeach()
