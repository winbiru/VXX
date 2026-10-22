# Pure V++ hosts file parser and File Machine ABI reader: no network required.
if (NOT DEFINED VPP_EXEC OR NOT EXISTS "${VPP_EXEC}")
    message(FATAL_ERROR "VPP_EXEC is missing")
endif()
if (NOT DEFINED VPP_SOURCE_ROOT)
    message(FATAL_ERROR "VPP_SOURCE_ROOT is missing")
endif()
set(fixture "${VPP_SOURCE_ROOT}/src/tests/kiem_tra_dns_hosts.vi")
file(READ "${VPP_SOURCE_ROOT}/src/tests/expected/kiem_tra_dns_hosts.expected" expected)
string(REPLACE "\r\n" "\n" expected "${expected}")
foreach(jit IN ITEMS 0 1)
    execute_process(
        COMMAND "${CMAKE_COMMAND}" -E env
            "VPP_HOME=${VPP_SOURCE_ROOT}" "VPP_ENABLE_JIT=${jit}"
            "${VPP_EXEC}" "${fixture}"
        WORKING_DIRECTORY "${VPP_SOURCE_ROOT}"
        RESULT_VARIABLE status
        OUTPUT_VARIABLE actual
        ERROR_VARIABLE diagnostics)
    string(REPLACE "\r\n" "\n" actual "${actual}")
    if (NOT status STREQUAL "0" OR NOT actual STREQUAL expected)
        message(FATAL_ERROR "DNS hosts JIT=${jit} failed (status=${status}):\n${actual}\n${diagnostics}")
    endif()
    message(STATUS "DNS hosts JIT=${jit}: PASS")
endforeach()
