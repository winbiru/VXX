# Resolve hostnames using DNS/Hosts V++ with no implicit libc resolver.
if (NOT DEFINED VPP_EXEC OR NOT EXISTS "${VPP_EXEC}")
    message(FATAL_ERROR "VPP_EXEC is missing")
endif()
if (NOT DEFINED VPP_SOURCE_ROOT OR NOT DEFINED VPP_BINARY_ROOT)
    message(FATAL_ERROR "Missing source or binary root")
endif()
if (NOT DEFINED VPP_COMPILER)
    set(VPP_COMPILER cpp)
endif()
set(compiler_environment "")
if (VPP_COMPILER STREQUAL "cpp")
    set(jit_modes 0 1)
elseif (VPP_COMPILER STREQUAL "vpp")
    if (NOT DEFINED VPP_NATIVE_COMPILER OR NOT EXISTS "${VPP_NATIVE_COMPILER}")
        message(FATAL_ERROR "Native DNS fixture requires a bootstrapped VPP_NATIVE_COMPILER")
    endif()
    set(compiler_environment "VPP_NATIVE_COMPILER=${VPP_NATIVE_COMPILER}")
    set(jit_modes 0)
else()
    message(FATAL_ERROR "Unsupported DNS fixture compiler: ${VPP_COMPILER}")
endif()
set(fixture "${VPP_SOURCE_ROOT}/src/tests/kiem_tra_socket_dns_vi.vi")
set(hosts_file "${VPP_BINARY_ROOT}/vpp-socket-dns-vi-${VPP_COMPILER}.hosts")
file(WRITE "${hosts_file}" "203.0.113.21 fixture.test\n::1 fixture.test\n")

foreach(jit IN LISTS jit_modes)
    foreach(mode IN ITEMS hosts none)
        set(hosts_path "${hosts_file}")
        if (mode STREQUAL "none")
            set(hosts_path "")
        endif()
        execute_process(
            COMMAND "${CMAKE_COMMAND}" -E env
                "VPP_HOME=${VPP_SOURCE_ROOT}" "VPP_ENABLE_JIT=${jit}"
                "VPP_SOCKET_VI_ONLY=1" "VPP_DNS_VI_HOSTS_FILE=${hosts_path}"
                "VPP_DNS_VI_SERVER=" "VPP_DNS_VI_SERVERS=" "VPP_DNS_VI_RESOLV_CONF="
                ${compiler_environment}
                "${VPP_EXEC}" run "--compiler=${VPP_COMPILER}" "${fixture}"
            WORKING_DIRECTORY "${VPP_BINARY_ROOT}"
            RESULT_VARIABLE status
            OUTPUT_VARIABLE actual
            ERROR_VARIABLE diagnostics TIMEOUT 180)
        string(REPLACE "\r\n" "\n" actual "${actual}")
        if (NOT status STREQUAL "0" OR NOT actual STREQUAL "1\n1\n1\n1\n1\n1\n")
            file(REMOVE "${hosts_file}")
            message(FATAL_ERROR "Socket DNS V++ compiler=${VPP_COMPILER}, JIT=${jit}, mode=${mode} failed (status=${status}):\n${actual}\n${diagnostics}")
        endif()
        message(STATUS "Socket DNS V++ compiler=${VPP_COMPILER}, JIT=${jit}, mode=${mode}: PASS")
    endforeach()
endforeach()
file(REMOVE "${hosts_file}")
