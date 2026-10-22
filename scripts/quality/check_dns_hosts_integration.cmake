# Verify resolver selection with explicit environment configurations, offline.
if (NOT DEFINED VPP_EXEC OR NOT EXISTS "${VPP_EXEC}")
    message(FATAL_ERROR "VPP_EXEC is missing")
endif()
if (NOT DEFINED VPP_SOURCE_ROOT OR NOT DEFINED VPP_BINARY_ROOT)
    message(FATAL_ERROR "Missing source or binary root")
endif()
set(fixture "${VPP_SOURCE_ROOT}/src/tests/kiem_tra_dns_hosts_resolver.vi")
set(hosts_file "${VPP_BINARY_ROOT}/vpp-dns-hosts-integration.hosts")
file(WRITE "${hosts_file}" "127.0.0.1 fixture.test\n::1 fixture.test\n")
foreach(jit IN ITEMS 0 1)
    foreach(mode IN ITEMS hosts hosts-only-miss hosts-dns-miss os-default)
        set(dns_server "")
        set(hosts_path "${hosts_file}")
        if (mode STREQUAL "hosts-dns-miss" OR mode STREQUAL "hosts")
            # A matched hosts entry must return before parsing an invalid DNS IP.
            set(dns_server "bad-ip")
        elseif(mode STREQUAL "os-default")
            set(hosts_path "")
        endif()
        execute_process(
            COMMAND "${CMAKE_COMMAND}" -E env
                "VPP_HOME=${VPP_SOURCE_ROOT}" "VPP_ENABLE_JIT=${jit}"
                "VPP_DNS_TEST_MODE=${mode}"
                "VPP_DNS_VI_HOSTS_FILE=${hosts_path}"
                "VPP_DNS_VI_SERVER=${dns_server}"
                "VPP_DNS_VI_SERVERS=" "VPP_DNS_VI_RESOLV_CONF="
                "${VPP_EXEC}" "${fixture}"
            WORKING_DIRECTORY "${VPP_BINARY_ROOT}"
            RESULT_VARIABLE status
            OUTPUT_VARIABLE actual
            ERROR_VARIABLE diagnostics)
        string(REPLACE "\r\n" "\n" actual "${actual}")
        if (NOT status STREQUAL "0" OR NOT actual STREQUAL "1\n")
            file(REMOVE "${hosts_file}")
            message(FATAL_ERROR "DNS hosts integration mode=${mode}, JIT=${jit} failed (status=${status}):\n${actual}\n${diagnostics}")
        endif()
        message(STATUS "DNS hosts integration mode=${mode} JIT=${jit}: PASS")
    endforeach()
endforeach()
file(REMOVE "${hosts_file}")
