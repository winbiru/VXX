# Cross-link socket ASM against a C test fixture (no C/C++ runtime component).
# A sandbox can deny bind(127.0.0.1) with EPERM; report SKIP in that case.
find_program(VPP_SOCKET_CLANG NAMES clang REQUIRED)
set(socket_dir "${VPP_OUTPUT_DIR}/socket-gate")
file(MAKE_DIRECTORY "${socket_dir}")
if(CMAKE_HOST_SYSTEM_NAME STREQUAL "Darwin")
    set(socket_arches arm64 x86_64)
elseif(CMAKE_HOST_SYSTEM_NAME STREQUAL "Linux" AND
       CMAKE_HOST_SYSTEM_PROCESSOR MATCHES "^(x86_64|AMD64)$")
    set(socket_arches x86_64)
elseif(CMAKE_HOST_SYSTEM_NAME STREQUAL "Linux" AND
       CMAKE_HOST_SYSTEM_PROCESSOR MATCHES "^(aarch64|arm64)$")
    set(socket_arches arm64)
else()
    message(STATUS "V++ socket VM/ASM: OS/CPU runtime harness not supported")
    return()
endif()
# Always exercise guest argument validation and secure failure of TLS,
# including in CI sandboxes that prohibit even 127.0.0.1 bind().
foreach(socket_case IN ITEMS invalid-port stale-token unverified-tls
    unverified-tls-send unverified-tls-recv)
    if(socket_case STREQUAL "invalid-port")
        set(guest_source "hàm chính() { máy đầu nối kết nối([127, 0, 0, 1], 0, 1, 1000); };\n")
        set(expected_error "error=2")
    elseif(socket_case STREQUAL "stale-token")
        set(guest_source "hàm chính() { máy đầu nối đóng(1073741825); };\n")
        set(expected_error "error=2")
    elseif(socket_case STREQUAL "unverified-tls")
        set(guest_source "hàm chính() { máy tls nâng cấp(0, \"example.com\"); };\n")
        set(expected_error "error=22 opcode=206")
    elseif(socket_case STREQUAL "unverified-tls-send")
        set(guest_source "hàm chính() { máy tls gửi(0, [65], 1); };\n")
        set(expected_error "error=22 opcode=207")
    else()
        set(guest_source "hàm chính() { máy tls nhận(0, [0], 1); };\n")
        set(expected_error "error=22 opcode=208")
    endif()
    set(guest_path "${socket_dir}/socket-${socket_case}.vi")
    set(image_path "${socket_dir}/socket-${socket_case}.vmb")
    file(WRITE "${guest_path}" "${guest_source}")
    execute_process(COMMAND "${VPP_NATIVE_COMPILER}" vm-build "${guest_path}" -o "${image_path}"
        RESULT_VARIABLE code OUTPUT_VARIABLE output ERROR_VARIABLE errors TIMEOUT 120)
    if(NOT code STREQUAL "0")
        message(FATAL_ERROR "Socket negative fixture ${socket_case} build: ${code}\n${output}\n${errors}")
    endif()
    execute_process(COMMAND "${CMAKE_COMMAND}" -E env "PATH="
        "${VPP_VM}" "${image_path}"
        RESULT_VARIABLE code OUTPUT_VARIABLE output ERROR_VARIABLE errors TIMEOUT 30)
    if(code STREQUAL "0" OR NOT errors MATCHES "${expected_error}")
        message(FATAL_ERROR "Socket negative fixture ${socket_case}: ${code}\n${output}\n${errors}")
    endif()
endforeach()
message(STATUS "V++ socket guest invalid arguments/stale token/TLS fail-closed 206..208: PASS")
set(any_executed FALSE)
foreach(socket_arch IN LISTS socket_arches)
    set(arch_flags "")
    set(prefix "")
    if(CMAKE_HOST_SYSTEM_NAME STREQUAL "Darwin")
        set(arch_flags -arch "${socket_arch}")
        if(socket_arch STREQUAL "x86_64" AND CMAKE_HOST_SYSTEM_PROCESSOR STREQUAL "arm64")
            execute_process(COMMAND /usr/bin/arch -x86_64 /usr/bin/true
                RESULT_VARIABLE rosetta_status ERROR_QUIET)
            if(NOT rosetta_status STREQUAL "0")
                message(STATUS "V++ socket x86_64: Rosetta unavailable, skipping runtime")
                continue()
            endif()
        endif()
        set(prefix /usr/bin/arch -${socket_arch})
    endif()
    set(socket_obj "${socket_dir}/socket-${socket_arch}.o")
    set(socket_bin "${socket_dir}/socket-${socket_arch}")
    execute_process(COMMAND "${VPP_SOCKET_CLANG}" ${arch_flags}
        -x assembler-with-cpp -c "${VPP_ROOT}/scripts/ASM/vpp_vm_socket_primitives.inc"
        -o "${socket_obj}"
        RESULT_VARIABLE code ERROR_VARIABLE errors TIMEOUT 30)
    if(NOT code STREQUAL "0")
        message(FATAL_ERROR "Socket ${socket_arch} ASM: ${code}\n${errors}")
    endif()
    execute_process(COMMAND "${VPP_SOCKET_CLANG}" ${arch_flags}
        -Wall -Wextra -Werror
        "${VPP_ROOT}/src/tests/fixtures/vm_asm_socket_harness.c"
        "${socket_obj}" -o "${socket_bin}"
        RESULT_VARIABLE code ERROR_VARIABLE errors TIMEOUT 30)
    if(NOT code STREQUAL "0")
        message(FATAL_ERROR "Socket ${socket_arch} harness link: ${code}\n${errors}")
    endif()
    execute_process(COMMAND ${prefix} "${socket_bin}"
        RESULT_VARIABLE code OUTPUT_VARIABLE output ERROR_VARIABLE errors TIMEOUT 35)
    if(code STREQUAL "77" AND output MATCHES "VM ASM SOCKET SKIP")
        message(STATUS "V++ socket ${socket_arch}: bind denied by runtime sandbox; execution skipped")
        continue()
    endif()
    if(NOT code STREQUAL "0" OR NOT output STREQUAL "VM ASM SOCKET OK\n")
        message(FATAL_ERROR "Socket ${socket_arch} runtime mismatch: ${code}\n${output}\n${errors}")
    endif()
    set(any_executed TRUE)
    message(STATUS "V++ socket ${socket_arch}: TCP/UDP, EINTR deadline, accept, FD cleanup PASS")
endforeach()
if(any_executed)
    execute_process(COMMAND "${VPP_NATIVE_COMPILER}" vm-build
        "${VPP_ROOT}/src/tests/fixtures/vm_asm_socket.vi"
        -o "${socket_dir}/socket-guest.vmb"
        RESULT_VARIABLE code OUTPUT_VARIABLE output ERROR_VARIABLE errors TIMEOUT 120)
    if(NOT code STREQUAL "0")
        message(FATAL_ERROR "Socket guest compile: ${code}\n${output}\n${errors}")
    endif()
    execute_process(COMMAND "${CMAKE_COMMAND}" -E env "PATH="
        "${VPP_VM}" "${socket_dir}/socket-guest.vmb"
        WORKING_DIRECTORY "${socket_dir}"
        RESULT_VARIABLE code OUTPUT_VARIABLE output ERROR_VARIABLE errors TIMEOUT 120)
    if(NOT code STREQUAL "0" OR NOT output STREQUAL "VM ASM SOCKET GUEST OK\n")
        message(FATAL_ERROR "Socket V++ guest mismatch: ${code}\n${output}\n${errors}")
    endif()
    message(STATUS "V++ socket VM guest opcode 185..192: PASS")
endif()
