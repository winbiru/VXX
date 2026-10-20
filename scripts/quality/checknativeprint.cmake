if (NOT DEFINED VPP_EXEC OR NOT DEFINED VPP_SOURCE OR
    NOT DEFINED VPP_TARGET OR NOT DEFINED VPP_OUTPUT_DIR OR NOT DEFINED VPP_HOME)
    message(FATAL_ERROR "missing native i32 runtime arguments")
endif()
file(MAKE_DIRECTORY "${VPP_OUTPUT_DIR}")
include("${CMAKE_CURRENT_LIST_DIR}/native_stage1_compile.cmake")
set(executable "${VPP_OUTPUT_DIR}/native print i32")
file(REMOVE "${executable}")
vpp_native_compile_command("${VPP_SOURCE}" "${executable}" compile_command)
execute_process(
    COMMAND "${CMAKE_COMMAND}" -E env "VPP_HOME=${VPP_HOME}"
        ${compile_command}
    WORKING_DIRECTORY "${VPP_OUTPUT_DIR}"
    RESULT_VARIABLE status OUTPUT_VARIABLE output ERROR_VARIABLE errors TIMEOUT 180)
if (NOT status EQUAL 0 OR NOT EXISTS "${executable}")
    message(FATAL_ERROR "native runtime link failed: ${status}\n${errors}\n${output}")
endif()
execute_process(COMMAND "${executable}"
    RESULT_VARIABLE status OUTPUT_VARIABLE output ERROR_VARIABLE errors TIMEOUT 30)
if (NOT status EQUAL 7 OR NOT output STREQUAL "-2147483648\n42\n0\n")
    message(FATAL_ERROR "native runtime output/exit mismatch: ${status}\n${errors}\n${output}")
endif()

# Stage 1 exposes only i32 printing, so reject invalid intrinsic arity.
# Stage 0 separately validates unsupported heap/string `in` diagnostics.
if (DEFINED VPP_STAGE1 AND NOT "${VPP_STAGE1}" STREQUAL "")
    file(WRITE "${VPP_OUTPUT_DIR}/string-print.vi"
        "hàm chính() { in_số(); trả về 0; };\n")
else()
    file(WRITE "${VPP_OUTPUT_DIR}/string-print.vi"
        "hàm chính() { in \"unsupported\"; trả về 0; };\n")
endif()
file(WRITE "${executable}" "preserve-native-output")
vpp_native_compile_command("${VPP_OUTPUT_DIR}/string-print.vi"
    "${executable}" compile_command)
execute_process(
    COMMAND "${CMAKE_COMMAND}" -E env "VPP_HOME=${VPP_HOME}"
        ${compile_command}
    WORKING_DIRECTORY "${VPP_OUTPUT_DIR}"
    RESULT_VARIABLE status ERROR_VARIABLE errors TIMEOUT 180)
file(READ "${executable}" content)
if (status EQUAL 0 OR NOT content STREQUAL "preserve-native-output" OR
    (NOT DEFINED VPP_STAGE1 AND NOT errors MATCHES "chưa hỗ trợ"))
    message(FATAL_ERROR "native runtime must reject non-i32 print safely: ${status}\n${errors}")
endif()

file(WRITE "${VPP_OUTPUT_DIR}/reserved-symbol.vi"
    "hàm vpp_native_print_i32(x) { trả về x; }; hàm main() { trả về 0; };\n")
vpp_native_compile_command("${VPP_OUTPUT_DIR}/reserved-symbol.vi"
    "${executable}" compile_command)
execute_process(
    COMMAND "${CMAKE_COMMAND}" -E env "VPP_HOME=${VPP_HOME}"
        ${compile_command}
    WORKING_DIRECTORY "${VPP_OUTPUT_DIR}"
    RESULT_VARIABLE status ERROR_VARIABLE errors TIMEOUT 180)
file(READ "${executable}" content)
if (status EQUAL 0 OR NOT content STREQUAL "preserve-native-output" OR
    (NOT DEFINED VPP_STAGE1 AND NOT errors MATCHES "dành riêng"))
    message(FATAL_ERROR "native runtime must reserve ABI symbols: ${status}\n${errors}")
endif()
message(STATUS "V++ native i32 runtime printing and rejection: PASS")
