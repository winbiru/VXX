# The test cases are .vi sources under src/tests. CMake only invokes the CLI
# and verifies compile rejection/dump stability; it cannot replace the compiler.
if (NOT DEFINED VPP_EXEC OR NOT DEFINED VPP_ROOT)
    message(FATAL_ERROR "missing VPP_EXEC or VPP_ROOT")
endif()

set(stress "${VPP_ROOT}/src/tests/kiem_tra_rc_hardening.vi")
set(malformed_dir "${VPP_ROOT}/src/tests/fixtures/rc_malformed")
file(GLOB malformed "${malformed_dir}/rc-malformed*.vi")
list(LENGTH malformed count)
if (count LESS 2)
    message(FATAL_ERROR "RC malformed corpus is missing: ${malformed_dir}")
endif()

execute_process(
    COMMAND "${CMAKE_COMMAND}" -E env "VPP_HOME=${VPP_ROOT}"
        "${VPP_EXEC}" dựng "${stress}"
    WORKING_DIRECTORY "${VPP_ROOT}"
    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
if (NOT result STREQUAL "0")
    message(FATAL_ERROR "RC stress source failed to compile: ${result}\n${error}\n${output}")
endif()

foreach(source IN LISTS malformed)
    execute_process(
        COMMAND "${CMAKE_COMMAND}" -E env "VPP_HOME=${VPP_ROOT}"
            "${VPP_EXEC}" dựng "${source}"
        WORKING_DIRECTORY "${VPP_ROOT}"
        RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
    if (NOT result STREQUAL "1")
        message(FATAL_ERROR "${source}: expected compiler exit 1, got ${result}\n${error}\n${output}")
    endif()
endforeach()

foreach(mode IN ITEMS --dump-ast --dump-ir --giải-mã)
    execute_process(
        COMMAND "${CMAKE_COMMAND}" -E env "VPP_HOME=${VPP_ROOT}"
            "${VPP_EXEC}" "${mode}" "${stress}"
        WORKING_DIRECTORY "${VPP_ROOT}"
        RESULT_VARIABLE first_result OUTPUT_VARIABLE first ERROR_VARIABLE first_error)
    execute_process(
        COMMAND "${CMAKE_COMMAND}" -E env "VPP_HOME=${VPP_ROOT}"
            "${VPP_EXEC}" "${mode}" "${stress}"
        WORKING_DIRECTORY "${VPP_ROOT}"
        RESULT_VARIABLE second_result OUTPUT_VARIABLE second ERROR_VARIABLE second_error)
    if (NOT first_result STREQUAL "0" OR NOT second_result STREQUAL "0" OR
        NOT first STREQUAL second)
        message(FATAL_ERROR "RC ${mode}: output is unstable or failed (${first_result}, ${second_result})\n${first_error}\n${second_error}")
    endif()
endforeach()
message(STATUS "RC compiler source corpus: ${count} malformed sources and deterministic dumps PASS")
