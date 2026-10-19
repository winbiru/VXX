if (NOT DEFINED VPP_EXEC OR NOT DEFINED VPP_ROOT OR NOT DEFINED VPP_OUTPUT_DIR)
    message(FATAL_ERROR "Bootstrap V++: missing CLI/root/output directory")
endif()
file(MAKE_DIRECTORY "${VPP_OUTPUT_DIR}")
set(artifact "${VPP_OUTPUT_DIR}/compiler.vbc")
execute_process(
    COMMAND "${CMAKE_COMMAND}" -E env "VPP_HOME=${VPP_ROOT}"
        "${VPP_EXEC}" build --compiler=vpp
        "${VPP_ROOT}/src/tests/kiemtrabootstrapvpp.vi" -o "${artifact}"
    WORKING_DIRECTORY "${VPP_OUTPUT_DIR}"
    RESULT_VARIABLE build_status OUTPUT_VARIABLE build_output ERROR_VARIABLE build_error
    TIMEOUT 1200)
if (NOT build_status EQUAL 0)
    message(FATAL_ERROR "Compiler V++ could not compile its bytecode core (${build_status}):\n${build_error}\n${build_output}")
endif()
execute_process(
    COMMAND "${CMAKE_COMMAND}" -E env "VPP_HOME=${VPP_ROOT}"
        "${VPP_EXEC}" run --artifact "${artifact}"
    WORKING_DIRECTORY "${VPP_OUTPUT_DIR}"
    RESULT_VARIABLE run_status OUTPUT_VARIABLE run_output ERROR_VARIABLE run_error
    TIMEOUT 600)
file(READ "${VPP_ROOT}/src/tests/expected/kiemtrabootstrapvpp.expected" expected)
string(REPLACE "\r\n" "\n" run_output "${run_output}")
if (NOT run_status EQUAL 0 OR NOT run_output STREQUAL expected)
    message(FATAL_ERROR "Generated compiler did not compile/run new source (${run_status}):\n${run_error}\n${run_output}")
endif()
message(STATUS "V++ bytecode core compiles itself and compiles/runs new source: PASS")
