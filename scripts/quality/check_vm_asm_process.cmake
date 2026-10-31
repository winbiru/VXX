# This is included after the standalone VM and native compiler are built.
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
file(WRITE "${VPP_OUTPUT_DIR}/process-asm-invalid.vi" 
    "hàm chính() { trả về máy tiến trình thô(4, -1); };\n")
execute_process(COMMAND "${VPP_STAGE1}" build "${VPP_OUTPUT_DIR}/process-asm-invalid.vi"
    -o "${VPP_OUTPUT_DIR}/process-asm-invalid"
    RESULT_VARIABLE status OUTPUT_VARIABLE output ERROR_VARIABLE errors TIMEOUT 120)
if (NOT status STREQUAL "0")
    message(FATAL_ERROR "Process ASM invalid-offset fixture compilation failed: ${status}\n${output}\n${errors}")
endif()
execute_process(COMMAND "${CMAKE_COMMAND}" -E env "PATH="
    "${VPP_OUTPUT_DIR}/process-asm-invalid"
    WORKING_DIRECTORY "${VPP_OUTPUT_DIR}"
    RESULT_VARIABLE status OUTPUT_VARIABLE output ERROR_VARIABLE errors TIMEOUT 30)
if (NOT status STREQUAL "234")
    message(FATAL_ERROR "Process ASM accepted negative arena offset: ${status}\n${output}\n${errors}")
endif()
include("${CMAKE_CURRENT_LIST_DIR}/check_vm_asm_process_x86.cmake")
message(STATUS "V++/ASM process: spawn, argv Unicode, pipes, exit and close PASS")
