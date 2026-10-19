foreach(required IN ITEMS VPP_CLI VPP_HOME VPP_SOURCE VPP_EXPECTED VPP_OUTPUT_DIR)
    if (NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "Native AOT installed smoke missing ${required}")
    endif()
endforeach()
if (NOT EXISTS "${VPP_EXPECTED}")
    message(FATAL_ERROR "Native AOT expected output missing: ${VPP_EXPECTED}")
endif()

file(MAKE_DIRECTORY "${VPP_OUTPUT_DIR}")
set(executable "${VPP_OUTPUT_DIR}/native-program")
execute_process(
    COMMAND "${CMAKE_COMMAND}" -E env "VPP_HOME=${VPP_HOME}"
        "${VPP_CLI}" --emit-native "${VPP_SOURCE}" "${executable}"
    WORKING_DIRECTORY "${VPP_OUTPUT_DIR}"
    RESULT_VARIABLE compile_status OUTPUT_VARIABLE compile_output
    ERROR_VARIABLE compile_errors TIMEOUT 120)
if (NOT compile_status STREQUAL "0" OR NOT EXISTS "${executable}")
    message(FATAL_ERROR "Native AOT compile failed (${compile_status}):\n${compile_output}\n${compile_errors}")
endif()

execute_process(COMMAND "${executable}"
    WORKING_DIRECTORY "${VPP_OUTPUT_DIR}"
    RESULT_VARIABLE run_status OUTPUT_VARIABLE run_output
    ERROR_VARIABLE run_errors TIMEOUT 30)
file(READ "${VPP_EXPECTED}" expected)
if (NOT run_status STREQUAL "0" OR NOT run_output STREQUAL expected)
    message(FATAL_ERROR "Native AOT parity failed (${run_status}):\nExpected:\n${expected}\nActual:\n${run_output}\n${run_errors}")
endif()
message(STATUS "Native AOT installed: ${VPP_SOURCE} PASS")
