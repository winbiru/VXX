# Run the native runtime fixture against the V++ runtime and ARM64 ASM.
if (NOT DEFINED VPP_EXEC OR NOT DEFINED VPP_ROOT OR NOT DEFINED VPP_TARGET OR
    NOT DEFINED VPP_OUTPUT_DIR)
    message(FATAL_ERROR "Native runtime: missing test inputs")
endif()
file(MAKE_DIRECTORY "${VPP_OUTPUT_DIR}")
set(source "${VPP_ROOT}/src/tests/fixtures/native_runtime.vi")
set(executable "${VPP_OUTPUT_DIR}/native-runtime-check")
execute_process(
    COMMAND "${CMAKE_COMMAND}" -E env "VPP_HOME=${VPP_ROOT}"
        "${VPP_EXEC}" build --target "${VPP_TARGET}" --emit-exe-selfhost
            "${source}" "${executable}"
    RESULT_VARIABLE status OUTPUT_VARIABLE output ERROR_VARIABLE errors TIMEOUT 180)
if (NOT status EQUAL 0 OR NOT EXISTS "${executable}")
    message(FATAL_ERROR "V++ native runtime compilation failed: ${status}\n${errors}\n${output}")
endif()
execute_process(COMMAND "${executable}" "" "Tiếng Việt"
    WORKING_DIRECTORY "${VPP_OUTPUT_DIR}"
    RESULT_VARIABLE status OUTPUT_VARIABLE output ERROR_VARIABLE errors TIMEOUT 60)
if (NOT status EQUAL 52 OR NOT output STREQUAL "-2147483648\n2147483647\n0\n")
    message(FATAL_ERROR "V++ native runtime failed: ${status}\n${errors}\n${output}")
endif()
file(READ "${VPP_OUTPUT_DIR}/runtime-owned.bin" actual HEX)
if (NOT actual STREQUAL "00ff0a34")
    message(FATAL_ERROR "V++ native runtime wrote incorrect bytes: ${actual}")
endif()
message(STATUS "V++ native memory/handle/file/argument runtime: PASS")
