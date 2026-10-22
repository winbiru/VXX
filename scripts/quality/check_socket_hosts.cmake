# Hostname resolution from an explicit hosts file must stay inside V++.
if (NOT DEFINED VPP_EXEC OR NOT EXISTS "${VPP_EXEC}")
    message(FATAL_ERROR "VPP_EXEC is missing")
endif()
if (NOT DEFINED VPP_SOURCE_ROOT OR NOT DEFINED VPP_BINARY_ROOT)
    message(FATAL_ERROR "Missing source or binary root")
endif()

set(fixture "${VPP_SOURCE_ROOT}/src/tests/kiem_tra_socket_hosts.vi")
file(READ "${VPP_SOURCE_ROOT}/src/tests/expected/kiem_tra_socket_hosts.expected" expected)
set(hosts_file "${VPP_BINARY_ROOT}/vpp-socket-hosts-fixture.hosts")
file(WRITE "${hosts_file}" "203.0.113.21 fixture.test alias.test\n::1 fixture.test\n0:0:0:0:0:0:0:1 fixture.test\n999.1.1.1 fixture.test\n")
foreach(jit IN ITEMS 0 1)
  foreach(vi_only IN ITEMS 0 1)
    execute_process(
        COMMAND "${CMAKE_COMMAND}" -E env
            "VPP_HOME=${VPP_SOURCE_ROOT}" "VPP_ENABLE_JIT=${jit}"
            "VPP_SOCKET_VI_ONLY=${vi_only}"
            "VPP_DNS_VI_HOSTS_FILE=${hosts_file}"
            "VPP_DNS_VI_SERVER=" "VPP_DNS_VI_SERVERS=" "VPP_DNS_VI_RESOLV_CONF="
            "${VPP_EXEC}" "${fixture}"
        WORKING_DIRECTORY "${VPP_BINARY_ROOT}"
        RESULT_VARIABLE status
        OUTPUT_VARIABLE actual
        ERROR_VARIABLE diagnostics)
    string(REPLACE "\r\n" "\n" actual "${actual}")
    set(expected_mode "${expected}")
    if (vi_only STREQUAL "1")
        string(APPEND expected_mode "1\n")
    endif()
    if (NOT status STREQUAL "0" OR NOT actual STREQUAL expected_mode)
        file(REMOVE "${hosts_file}")
        message(FATAL_ERROR "Socket V++ hosts JIT=${jit}, VI_ONLY=${vi_only} failed (status=${status}):\n${actual}\n${diagnostics}")
    endif()
    message(STATUS "Socket V++ hosts JIT=${jit}, VI_ONLY=${vi_only}: PASS")
  endforeach()
endforeach()
file(REMOVE "${hosts_file}")

# Không có tệp hosts vẫn phải fail-closed trước mọi lời gọi resolver C.
foreach(jit IN ITEMS 0 1)
    execute_process(
        COMMAND "${CMAKE_COMMAND}" -E env
            "VPP_HOME=${VPP_SOURCE_ROOT}" "VPP_ENABLE_JIT=${jit}"
            "VPP_SOCKET_VI_ONLY=1" "VPP_DNS_VI_HOSTS_FILE="
            "VPP_DNS_VI_SERVER=" "VPP_DNS_VI_SERVERS=" "VPP_DNS_VI_RESOLV_CONF="
            "${VPP_EXEC}" "${VPP_SOURCE_ROOT}/src/tests/kiem_tra_socket_vi_only.vi"
        WORKING_DIRECTORY "${VPP_BINARY_ROOT}"
        RESULT_VARIABLE status
        OUTPUT_VARIABLE actual
        ERROR_VARIABLE diagnostics)
    string(REPLACE "\r\n" "\n" actual "${actual}")
    if (NOT status STREQUAL "0" OR NOT actual STREQUAL "1\n1\n1\n")
        message(FATAL_ERROR "Socket VI_ONLY without hosts JIT=${jit} failed (status=${status}):\n${actual}\n${diagnostics}")
    endif()
    message(STATUS "Socket VI_ONLY without hosts JIT=${jit}: PASS")
endforeach()
