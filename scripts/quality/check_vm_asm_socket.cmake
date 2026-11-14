# V++ guest checks replace the deleted C host harness. Negative checks require
# no network access; the UDP guest exercises token ownership, timeout, send and
# close through the real ASM boundary. A denied socket is a failure, not PASS.
include("${CMAKE_CURRENT_LIST_DIR}/check_vm_asm_socket_negative.cmake")
set(socket_image "${socket_dir}/socket-guest.vmb")
execute_process(COMMAND "${VPP_NATIVE_COMPILER}" vm-build
    "${VPP_ROOT}/scripts/test/fixtures/vm_asm_socket.vi" -o "${socket_image}"
    RESULT_VARIABLE code OUTPUT_VARIABLE output ERROR_VARIABLE errors TIMEOUT 120)
if(NOT code STREQUAL "0")
    message(FATAL_ERROR "Socket guest compile: ${code}\n${output}\n${errors}")
endif()
execute_process(COMMAND "${CMAKE_COMMAND}" -E env "PATH=" "${VPP_VM}" "${socket_image}"
    WORKING_DIRECTORY "${socket_dir}"
    RESULT_VARIABLE code OUTPUT_VARIABLE output ERROR_VARIABLE errors TIMEOUT 120)
if(NOT code STREQUAL "0" OR NOT output STREQUAL "VM ASM SOCKET GUEST OK\n")
    message(FATAL_ERROR "Socket V++ guest mismatch: ${code}\n${output}\n${errors}")
endif()
message(STATUS "V++ socket guest: UDP send, token ownership, timeout and close PASS")
