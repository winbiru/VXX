if (NOT DEFINED VPP_EXEC OR NOT DEFINED VPP_ROOT OR NOT DEFINED VPP_OUTPUT_DIR)
    message(FATAL_ERROR "Stage 1/2 bootstrap: missing CLI/root/output directory")
endif()

file(MAKE_DIRECTORY "${VPP_OUTPUT_DIR}")
set(stage1_source "${VPP_OUTPUT_DIR}/bootstrap-stage1.vi")
set(stage2 "${VPP_OUTPUT_DIR}/stage2.vbc")
set(probe "${VPP_OUTPUT_DIR}/stage2-probe.vbc")

# CMake packages the source graph; compilation of Stage 2 is V++ code.
execute_process(
    COMMAND "${CMAKE_COMMAND}"
        "-DVPP_ROOT=${VPP_ROOT}"
        "-DVPP_STAGE1_SOURCE=${stage1_source}"
        "-DVPP_STAGE2_SOURCE=${VPP_ROOT}/src/tests/bootstrapstage2.vi"
        -P "${VPP_ROOT}/scripts/quality/prepare_bootstrap_stages.cmake"
    RESULT_VARIABLE status OUTPUT_VARIABLE output ERROR_VARIABLE errors TIMEOUT 30)
if (NOT status EQUAL 0)
    message(FATAL_ERROR "Stage 1 source graph generation failed: ${errors}\n${output}")
endif()
message(STATUS "Bootstrap source graph ready: ${stage1_source}")

# Stage 0 compiles/runs Stage 1 from its V++ source using the C++ bootstrap
# runtime. Stage 1 performs all compiler passes in V++ and emits Stage 2.
# Stage 2 is then loaded as an independent bytecode compiler artifact.
if (NOT VPP_BOOTSTRAP_REUSE_STAGE2)
    message(STATUS "Bootstrap Stage 1 -> Stage 2: compiler written in V++")
    execute_process(
        COMMAND "${CMAKE_COMMAND}" -E env "VPP_HOME=${VPP_ROOT}"
            "${VPP_EXEC}" run --compiler=cpp "${stage1_source}"
        WORKING_DIRECTORY "${VPP_ROOT}"
        RESULT_VARIABLE status OUTPUT_VARIABLE output ERROR_VARIABLE errors TIMEOUT 1200)
    if (NOT status EQUAL 0 OR NOT output MATCHES "\"định dạng\":\"VPP bytecode\"")
        message(FATAL_ERROR "Stage 1 -> Stage 2 compile failed: ${status}\n${errors}\n${output}")
    endif()
    string(REGEX REPLACE "\r?\n$" "" output "${output}")
    file(WRITE "${stage2}" "${output}")
endif()
if (NOT EXISTS "${stage2}")
    message(FATAL_ERROR "Stage 2 artifact missing: ${stage2}")
endif()
file(SHA256 "${stage2}" stage2_sha)
message(STATUS "Bootstrap Stage 2 ready: SHA256 ${stage2_sha}")

# Stage 2 must itself compile a fresh source containing defaults and loops.
message(STATUS "Bootstrap Stage 2 -> probe: compiling fresh V++ source")
execute_process(
    COMMAND "${CMAKE_COMMAND}" -E env "VPP_HOME=${VPP_ROOT}"
        "${VPP_EXEC}" run --artifact "${stage2}"
    WORKING_DIRECTORY "${VPP_ROOT}"
    RESULT_VARIABLE status OUTPUT_VARIABLE output ERROR_VARIABLE errors TIMEOUT 1200)
if (NOT status EQUAL 0 OR NOT output MATCHES "\"định dạng\":\"VPP bytecode\"")
    message(FATAL_ERROR "Stage 2 compiler execution failed: ${status}\n${errors}\n${output}")
endif()
string(REGEX REPLACE "\r?\n$" "" output "${output}")
file(WRITE "${probe}" "${output}")
execute_process(
    COMMAND "${CMAKE_COMMAND}" -E env "VPP_HOME=${VPP_ROOT}"
        "${VPP_EXEC}" run --artifact "${probe}"
    WORKING_DIRECTORY "${VPP_ROOT}"
    RESULT_VARIABLE status OUTPUT_VARIABLE output ERROR_VARIABLE errors TIMEOUT 120)
if (NOT status EQUAL 0 OR NOT output STREQUAL "52\n")
    message(FATAL_ERROR "Stage 2 generated invalid bytecode: ${status}\n${errors}\n${output}")
endif()
message(STATUS "Stage 1 -> Stage 2 compiler PASS; Stage 2 compiled probe = 52; SHA256 ${stage2_sha}")
