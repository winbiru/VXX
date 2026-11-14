# This is included after the standalone VM and native compiler are built.
if(WIN32)
    # Exercise Win32 pipes, environment, missing executables and JobObject timeout
    # through the standalone VM, against the same goldens as native AOT.
    foreach(stem IN ITEMS kiem_tra_ffi_process_spawn_windows kiem_tra_ffi_process_timeout_windows)
        set(image "${VPP_OUTPUT_DIR}/${stem}.vmb")
        execute_process(COMMAND "${VPP_NATIVE_COMPILER}" vm-build
            "${VPP_ROOT}/src/tests/${stem}.vi" -o "${image}"
            RESULT_VARIABLE status OUTPUT_VARIABLE output ERROR_VARIABLE errors TIMEOUT 120)
        if(NOT status STREQUAL "0")
            message(FATAL_ERROR "Windows process VM compile: ${status}\n${output}\n${errors}")
        endif()
        file(READ "${VPP_ROOT}/src/tests/expected/${stem}.expected" expected)
        execute_process(COMMAND "${CMAKE_COMMAND}" -E env "PATH=$ENV{SystemRoot}/System32" "${VPP_VM}" "${image}"
            WORKING_DIRECTORY "${VPP_OUTPUT_DIR}"
            RESULT_VARIABLE status OUTPUT_VARIABLE output ERROR_VARIABLE errors TIMEOUT 120)
        if(NOT status STREQUAL "0" OR NOT output STREQUAL expected)
            message(FATAL_ERROR "Windows process VM ${stem}: ${status}\n${output}\n${errors}")
        endif()
    endforeach()
    # An independent Python child checks exact UTF-16 argv and environment,
    # fills both pipes, closes pipes before exit, and verifies a descendant
    # really terminates after the V++ JobObject deadline.
    set(probe_source "${VPP_ROOT}/src/tests/fixtures/vm_asm_process_windows.vi")
    set(probe_script "${VPP_ROOT}/scripts/quality/windows_process_probe.py")
    set(probe_image "${VPP_OUTPUT_DIR}/process-windows.vmb")
    set(probe_native "${VPP_OUTPUT_DIR}/process-windows.exe")
    foreach(mode IN ITEMS vm-build build)
        if(mode STREQUAL "vm-build")
            set(output_path "${probe_image}")
            set(run_command "${VPP_VM}" "${probe_image}")
        else()
            set(output_path "${probe_native}")
            set(run_command "${probe_native}")
        endif()
        execute_process(COMMAND "${VPP_NATIVE_COMPILER}" "${mode}" "${probe_source}" -o "${output_path}"
            RESULT_VARIABLE status OUTPUT_VARIABLE output ERROR_VARIABLE errors TIMEOUT 600)
        if(NOT status STREQUAL "0")
            message(FATAL_ERROR "Windows process oracle ${mode} compile: ${status}\n${output}\n${errors}")
        endif()
        execute_process(COMMAND "${CMAKE_COMMAND}" -E env "PATH=$ENV{SystemRoot}/System32"
            "VPP_PROCESS_TEST_PYTHON=${VPP_PYTHON}" "VPP_PROCESS_TEST_PROBE=${probe_script}" ${run_command}
            WORKING_DIRECTORY "${VPP_OUTPUT_DIR}"
            RESULT_VARIABLE status OUTPUT_VARIABLE output ERROR_VARIABLE errors TIMEOUT 120)
        if(NOT status STREQUAL "0" OR NOT output STREQUAL "WINDOWS PROCESS OK\n")
            message(FATAL_ERROR "Windows process oracle ${mode}: ${status}\n${output}\n${errors}")
        endif()
    endforeach()
else()
set(process_fixture "${VPP_ROOT}/src/tests/fixtures/vm_asm_process.vi")
set(process_image "${VPP_OUTPUT_DIR}/process.vmb")
execute_process(COMMAND "${VPP_NATIVE_COMPILER}" vm-build "${process_fixture}" -o "${process_image}"
    RESULT_VARIABLE status OUTPUT_VARIABLE output ERROR_VARIABLE errors TIMEOUT 120)
if (NOT status STREQUAL "0")
    message(FATAL_ERROR "Process VM fixture compilation failed (${status}):\n${output}\n${errors}")
endif()
execute_process(COMMAND "${CMAKE_COMMAND}" -E env "PATH=" "${VPP_VM}" "${process_image}"
    WORKING_DIRECTORY "${VPP_OUTPUT_DIR}"
    RESULT_VARIABLE status OUTPUT_VARIABLE output ERROR_VARIABLE errors TIMEOUT 120)
if (NOT status STREQUAL "0" OR NOT output STREQUAL "VM ASM PROCESS OK\n")
    message(FATAL_ERROR "Process VM/ASM spawn, pipe and wait regression (${status}):\n${output}\n${errors}")
endif()
endif()
file(WRITE "${VPP_OUTPUT_DIR}/process-stale.vi" [=[hàm chính() {
    p = máy tiến trình tạo("/bin/echo");
    máy tiến trình đóng(p);
    máy tiến trình đóng(p);
};
]=])
execute_process(COMMAND "${VPP_NATIVE_COMPILER}" vm-build
    "${VPP_OUTPUT_DIR}/process-stale.vi" -o "${VPP_OUTPUT_DIR}/process-stale.vmb"
    RESULT_VARIABLE status OUTPUT_VARIABLE output ERROR_VARIABLE errors TIMEOUT 120)
if (NOT status STREQUAL "0")
    message(FATAL_ERROR "Process stale-token fixture compilation failed: ${status}\n${output}\n${errors}")
endif()
execute_process(COMMAND "${CMAKE_COMMAND}" -E env "PATH=" "${VPP_VM}"
    "${VPP_OUTPUT_DIR}/process-stale.vmb"
    WORKING_DIRECTORY "${VPP_OUTPUT_DIR}"
    RESULT_VARIABLE status OUTPUT_VARIABLE output ERROR_VARIABLE errors TIMEOUT 30)
if (status STREQUAL "0" OR NOT errors MATCHES "error=2")
    message(FATAL_ERROR "Process VM accepted closed/stale token: ${status}\n${output}\n${errors}")
endif()
# POSIX argv/env must reject malformed UTF-8, embedded NUL and invalid
# environment keys without attempting to launch a process.
set(process_invalid_cases
    "máy tiến trình tạo(chuỗi từ byte máy ảo([192, 175]))"
    "máy tiến trình tạo(chuỗi từ byte máy ảo([47, 98, 105, 110, 0, 115, 104]))"
    "p = máy tiến trình tạo(\"/bin/echo\")\; máy tiến trình đối số(p, chuỗi từ byte máy ảo([237, 160, 128]))"
    "p = máy tiến trình tạo(\"/bin/echo\")\; máy tiến trình môi trường(p, \"BAD=KEY\", \"value\")")
set(process_invalid_index 0)
foreach(expression IN LISTS process_invalid_cases)
    math(EXPR process_invalid_index "${process_invalid_index} + 1")
    set(bad_source "${VPP_OUTPUT_DIR}/process-invalid-${process_invalid_index}.vi")
    set(bad_image "${VPP_OUTPUT_DIR}/process-invalid-${process_invalid_index}.vmb")
    file(WRITE "${bad_source}" "hàm chính() { ${expression}; };\n")
    execute_process(COMMAND "${VPP_NATIVE_COMPILER}" vm-build "${bad_source}" -o "${bad_image}"
        RESULT_VARIABLE code OUTPUT_VARIABLE output ERROR_VARIABLE errors TIMEOUT 60)
    if(NOT code STREQUAL "0")
        message(FATAL_ERROR "Process invalid-${process_invalid_index} compile: ${code}\n${output}\n${errors}")
    endif()
    execute_process(COMMAND "${CMAKE_COMMAND}" -E env "PATH=" "${VPP_VM}" "${bad_image}"
        WORKING_DIRECTORY "${VPP_OUTPUT_DIR}"
        RESULT_VARIABLE code OUTPUT_VARIABLE output ERROR_VARIABLE errors TIMEOUT 30)
    if(code STREQUAL "0" OR NOT errors MATCHES "error=2")
        message(FATAL_ERROR "Process invalid-${process_invalid_index} accepted: ${code}\n${output}\n${errors}")
    endif()
endforeach()
set(raw_process "${VPP_OUTPUT_DIR}/process-asm-invalid")
set(raw_exit "234")
if(WIN32)
    string(APPEND raw_process ".exe")
    set(raw_exit "4294967274")
endif()
file(WRITE "${VPP_OUTPUT_DIR}/process-asm-invalid.vi" 
    "hàm chính() { trả về máy tiến trình thô(4, -1); };\n")
vpp_raw_build("${VPP_OUTPUT_DIR}/process-asm-invalid.vi" "${raw_process}")
execute_process(COMMAND "${CMAKE_COMMAND}" -E env "PATH="
    "${raw_process}"
    WORKING_DIRECTORY "${VPP_OUTPUT_DIR}"
    RESULT_VARIABLE status OUTPUT_VARIABLE output ERROR_VARIABLE errors TIMEOUT 30)
if (NOT status STREQUAL raw_exit AND NOT (WIN32 AND status STREQUAL "-22"))
    message(FATAL_ERROR "Process ASM accepted negative arena offset: ${status}\n${output}\n${errors}")
endif()

if(WIN32)
    message(STATUS "V++/ASM Windows process: spawn, pipes, environment, timeout and close PASS")
else()
    message(STATUS "V++/ASM process: spawn, argv Unicode, pipes, exit and close PASS")
endif()
