if (NOT DEFINED VPP_EXEC OR NOT DEFINED VPP_SOURCE OR
    NOT DEFINED VPP_TARGET OR NOT DEFINED VPP_OUTPUT_DIR OR NOT DEFINED VPP_HOME)
    message(FATAL_ERROR "missing native i32 runtime arguments")
endif()
file(MAKE_DIRECTORY "${VPP_OUTPUT_DIR}")
set(executable "${VPP_OUTPUT_DIR}/native print i32")
execute_process(
    COMMAND "${CMAKE_COMMAND}" -E env "VPP_HOME=${VPP_HOME}"
        "${VPP_EXEC}" build --target "${VPP_TARGET}" --emit-exe-selfhost
        "${VPP_SOURCE}" "${executable}"
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

# Unsupported heap/string printing must fail without overwriting an executable.
file(WRITE "${VPP_OUTPUT_DIR}/string-print.vi"
    "hàm chính() { in \"unsupported\"; trả về 0; };\n")
file(WRITE "${executable}" "preserve-native-output")
execute_process(
    COMMAND "${CMAKE_COMMAND}" -E env "VPP_HOME=${VPP_HOME}"
        "${VPP_EXEC}" build --target "${VPP_TARGET}" --emit-exe-selfhost
        "${VPP_OUTPUT_DIR}/string-print.vi" "${executable}"
    WORKING_DIRECTORY "${VPP_OUTPUT_DIR}"
    RESULT_VARIABLE status ERROR_VARIABLE errors TIMEOUT 180)
file(READ "${executable}" content)
if (status EQUAL 0 OR NOT content STREQUAL "preserve-native-output" OR
    NOT errors MATCHES "chưa hỗ trợ")
    message(FATAL_ERROR "native runtime must reject non-i32 print safely: ${status}\n${errors}")
endif()

file(WRITE "${VPP_OUTPUT_DIR}/reserved-symbol.vi"
    "hàm vpp_native_print_i32(x) { trả về x; }; hàm main() { in 1; trả về 0; };\n")
execute_process(
    COMMAND "${CMAKE_COMMAND}" -E env "VPP_HOME=${VPP_HOME}"
        "${VPP_EXEC}" build --target "${VPP_TARGET}" --emit-exe-selfhost
        "${VPP_OUTPUT_DIR}/reserved-symbol.vi" "${executable}"
    WORKING_DIRECTORY "${VPP_OUTPUT_DIR}"
    RESULT_VARIABLE status ERROR_VARIABLE errors TIMEOUT 180)
file(READ "${executable}" content)
if (status EQUAL 0 OR NOT content STREQUAL "preserve-native-output" OR
    NOT errors MATCHES "dành riêng")
    message(FATAL_ERROR "native runtime must reserve ABI symbols: ${status}\n${errors}")
endif()
message(STATUS "V++ native i32 runtime printing and rejection: PASS")
