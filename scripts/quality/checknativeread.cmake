if (NOT DEFINED VPP_EXEC OR NOT DEFINED VPP_SOURCE OR
    NOT DEFINED VPP_TARGET OR NOT DEFINED VPP_OUTPUT_DIR OR NOT DEFINED VPP_HOME)
    message(FATAL_ERROR "missing native input ABI arguments")
endif()
file(MAKE_DIRECTORY "${VPP_OUTPUT_DIR}")
include("${CMAKE_CURRENT_LIST_DIR}/native_stage1_compile.cmake")
set(executable "${VPP_OUTPUT_DIR}/native read i32")
file(REMOVE "${executable}")
vpp_native_compile_command("${VPP_SOURCE}" "${executable}" compile_command)
execute_process(
    COMMAND "${CMAKE_COMMAND}" -E env "VPP_HOME=${VPP_HOME}"
        ${compile_command}
    WORKING_DIRECTORY "${VPP_OUTPUT_DIR}"
    RESULT_VARIABLE status ERROR_VARIABLE errors TIMEOUT 180)
if (NOT status EQUAL 0 OR NOT EXISTS "${executable}")
    message(FATAL_ERROR "native input compile failed: ${status}\n${errors}")
endif()

foreach(case IN ITEMS boundary invalid overflow eof longline whitespace)
    if (case STREQUAL "boundary")
        set(input "-2147483648\n2147483647\n")
        set(expected "-2147483648\n0\n2147483647\n0\n")
    elseif (case STREQUAL "invalid")
        set(input "12abc\n-1\n")
        set(expected "0\n2\n-1\n0\n")
    elseif (case STREQUAL "overflow")
        set(input "2147483648\n0\n")
        set(expected "0\n3\n0\n0\n")
    elseif (case STREQUAL "longline")
        string(REPEAT "9" 80 digits)
        set(input "${digits}\n17\n")
        set(expected "0\n2\n17\n0\n")
    elseif (case STREQUAL "whitespace")
        set(input "  +42  \n-0\n")
        set(expected "42\n0\n0\n0\n")
    else()
        set(input "")
        set(expected "0\n1\n0\n1\n")
    endif()
    set(input_file "${VPP_OUTPUT_DIR}/${case}.txt")
    file(WRITE "${input_file}" "${input}")
    execute_process(COMMAND "${executable}" INPUT_FILE "${input_file}"
        RESULT_VARIABLE status OUTPUT_VARIABLE output ERROR_VARIABLE errors TIMEOUT 30)
    if (NOT status EQUAL 0 OR NOT output STREQUAL expected)
        message(FATAL_ERROR "native read ${case}: ${status}\n${errors}\n${output}")
    endif()
endforeach()

# Bad arity and reserved symbols must be rejected before publishing output.
file(WRITE "${VPP_OUTPUT_DIR}/wrong-arity.vi"
    "hàm chính() { trả về đọc_số(12); };\n")
file(REMOVE "${VPP_OUTPUT_DIR}/bad-output")
vpp_native_compile_command("${VPP_OUTPUT_DIR}/wrong-arity.vi"
    "${VPP_OUTPUT_DIR}/bad-output" compile_command)
execute_process(
    COMMAND "${CMAKE_COMMAND}" -E env "VPP_HOME=${VPP_HOME}"
        ${compile_command}
    RESULT_VARIABLE status ERROR_VARIABLE errors TIMEOUT 180)
if (status EQUAL 0 OR EXISTS "${VPP_OUTPUT_DIR}/bad-output" OR
    (NOT DEFINED VPP_STAGE1 AND NOT errors MATCHES "không nhận đối số"))
    message(FATAL_ERROR "native input should reject wrong arity: ${status}\n${errors}")
endif()
foreach(symbol IN ITEMS đọc_số vpp_native_read_i32 vpp_native_read_status)
    file(WRITE "${VPP_OUTPUT_DIR}/reserved-symbol.vi"
        "hàm ${symbol}() { trả về 0; }; hàm chính() { trả về 0; };\n")
    file(REMOVE "${VPP_OUTPUT_DIR}/bad-output")
    vpp_native_compile_command("${VPP_OUTPUT_DIR}/reserved-symbol.vi"
        "${VPP_OUTPUT_DIR}/bad-output" compile_command)
    execute_process(
        COMMAND "${CMAKE_COMMAND}" -E env "VPP_HOME=${VPP_HOME}"
            ${compile_command}
        RESULT_VARIABLE status ERROR_VARIABLE errors TIMEOUT 180)
    if (status EQUAL 0 OR EXISTS "${VPP_OUTPUT_DIR}/bad-output" OR
        (NOT DEFINED VPP_STAGE1 AND NOT errors MATCHES "dành riêng"))
        message(FATAL_ERROR "native input must reserve ${symbol}: ${status}\n${errors}")
    endif()
endforeach()
message(STATUS "V++ native i32 input and status ABI: PASS")
