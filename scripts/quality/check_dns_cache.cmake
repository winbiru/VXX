# Offline V++ cache contract, tested in interpreter and JIT.
if (NOT DEFINED VPP_EXEC OR NOT EXISTS "${VPP_EXEC}")
    message(FATAL_ERROR "VPP_EXEC is missing")
endif()
if (NOT DEFINED VPP_SOURCE_ROOT)
    message(FATAL_ERROR "VPP_SOURCE_ROOT is missing")
endif()
set(fixture "${VPP_SOURCE_ROOT}/src/tests/kiem_tra_dns_cache.vi")
file(READ "${VPP_SOURCE_ROOT}/src/tests/expected/kiem_tra_dns_cache.expected" expected)
foreach(jit IN ITEMS 0 1)
    execute_process(
        COMMAND "${CMAKE_COMMAND}" -E env
            "VPP_HOME=${VPP_SOURCE_ROOT}" "VPP_ENABLE_JIT=${jit}"
            "${VPP_EXEC}" "${fixture}"
        WORKING_DIRECTORY "${VPP_SOURCE_ROOT}"
        RESULT_VARIABLE result_code
        OUTPUT_VARIABLE actual
        ERROR_VARIABLE diagnostics)
    string(REPLACE "\r\n" "\n" actual "${actual}")
    if (NOT result_code STREQUAL "0" OR NOT actual STREQUAL expected)
        message(FATAL_ERROR "DNS cache JIT=${jit} failed (status=${result_code}):\n${actual}\n${diagnostics}")
    endif()
    message(STATUS "DNS cache JIT=${jit}: PASS")
endforeach()
