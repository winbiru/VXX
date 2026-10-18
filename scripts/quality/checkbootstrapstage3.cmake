# Extended self-hosting gate: C++ bootstrap -> V++ Stage 2 -> V++ Stage 3
# -> fresh bytecode probe. Each compiler pass at Stage 2 and Stage 3 runs
# inside its respective V++ bytecode artifact.
if (NOT DEFINED VPP_EXEC OR NOT DEFINED VPP_ROOT OR NOT DEFINED VPP_OUTPUT_DIR)
    message(FATAL_ERROR "Stage 1/2/3 bootstrap: missing CLI/root/output directory")
endif()

file(MAKE_DIRECTORY "${VPP_OUTPUT_DIR}")
set(stage1_source "${VPP_OUTPUT_DIR}/bootstrap-stage1.vi")
set(stage2_source "${VPP_OUTPUT_DIR}/bootstrap-stage2.vi")
set(stage2 "${VPP_OUTPUT_DIR}/stage2.vbc")
set(stage3 "${VPP_OUTPUT_DIR}/stage3.vbc")
set(probe "${VPP_OUTPUT_DIR}/stage3-probe.vbc")

# Source packaging only. The second invocation embeds the complete source
# graph for the Stage 3 compiler into the Stage 2 compiler's V++ program.
# Neither invocation performs a compiler pass.
execute_process(
    COMMAND "${CMAKE_COMMAND}"
        "-DVPP_ROOT=${VPP_ROOT}"
        "-DVPP_STAGE1_SOURCE=${stage2_source}"
        "-DVPP_STAGE2_SOURCE=${VPP_ROOT}/src/tests/bootstrapstage2.vi"
        -P "${VPP_ROOT}/scripts/quality/prepare_bootstrap_stages.cmake"
    RESULT_VARIABLE status OUTPUT_VARIABLE output ERROR_VARIABLE errors TIMEOUT 30)
if (NOT status EQUAL 0)
    message(FATAL_ERROR "Stage 2 source generation failed: ${status}\n${errors}\n${output}")
endif()
execute_process(
    COMMAND "${CMAKE_COMMAND}"
        "-DVPP_ROOT=${VPP_ROOT}"
        "-DVPP_STAGE1_SOURCE=${stage1_source}"
        "-DVPP_STAGE2_SOURCE=${stage2_source}"
        -P "${VPP_ROOT}/scripts/quality/prepare_bootstrap_stages.cmake"
    RESULT_VARIABLE status OUTPUT_VARIABLE output ERROR_VARIABLE errors TIMEOUT 30)
if (NOT status EQUAL 0)
    message(FATAL_ERROR "Stage 1 source generation failed: ${status}\n${errors}\n${output}")
endif()

if (NOT VPP_BOOTSTRAP_REUSE_STAGE2)
    message(STATUS "Stage 1 -> Stage 2: compiling complete V++ compiler source graph")
    execute_process(
        COMMAND "${CMAKE_COMMAND}" -E env "VPP_HOME=${VPP_ROOT}"
            "${VPP_EXEC}" run --compiler=cpp "${stage1_source}"
        WORKING_DIRECTORY "${VPP_ROOT}"
        RESULT_VARIABLE status OUTPUT_VARIABLE output ERROR_VARIABLE errors TIMEOUT 1200)
    if (NOT status EQUAL 0 OR NOT output MATCHES "\"định dạng\":\"VPP bytecode\"")
        message(FATAL_ERROR "Stage 1 -> Stage 2 failed: ${status}\n${errors}\n${output}")
    endif()
    string(REGEX REPLACE "\r?\n$" "" output "${output}")
    file(WRITE "${stage2}" "${output}")
endif()
if (NOT EXISTS "${stage2}")
    message(FATAL_ERROR "Stage 2 artifact missing: ${stage2}")
endif()
file(SHA256 "${stage2}" stage2_sha)
message(STATUS "Stage 2 ready: ${stage2_sha}")

message(STATUS "Stage 2 -> Stage 3: compiling V++ compiler with V++ bytecode")
execute_process(
    COMMAND "${CMAKE_COMMAND}" -E env "VPP_HOME=${VPP_ROOT}"
        "${VPP_EXEC}" run --artifact "${stage2}"
    WORKING_DIRECTORY "${VPP_ROOT}"
    RESULT_VARIABLE status OUTPUT_VARIABLE output ERROR_VARIABLE errors TIMEOUT 1200)
if (NOT status EQUAL 0 OR NOT output MATCHES "\"định dạng\":\"VPP bytecode\"")
    message(FATAL_ERROR "Stage 2 -> Stage 3 failed: ${status}\n${errors}\n${output}")
endif()
string(REGEX REPLACE "\r?\n$" "" output "${output}")
file(WRITE "${stage3}" "${output}")
file(SHA256 "${stage3}" stage3_sha)
if (DEFINED VPP_BOOTSTRAP_STAGE3_ORACLE)
    if (NOT EXISTS "${VPP_BOOTSTRAP_STAGE3_ORACLE}")
        message(FATAL_ERROR "Stage 3 oracle missing: ${VPP_BOOTSTRAP_STAGE3_ORACLE}")
    endif()
    file(SHA256 "${VPP_BOOTSTRAP_STAGE3_ORACLE}" oracle_sha)
    if (NOT stage3_sha STREQUAL oracle_sha)
        message(FATAL_ERROR "Stage 3 compiler differs from Stage 1 oracle: ${stage3_sha} != ${oracle_sha}")
    endif()
    message(STATUS "Stage 3 compiler reproducible against Stage 1 oracle: ${oracle_sha}")
endif()

message(STATUS "Stage 3 -> probe: compiling independent V++ source")
execute_process(
    COMMAND "${CMAKE_COMMAND}" -E env "VPP_HOME=${VPP_ROOT}"
        "${VPP_EXEC}" run --artifact "${stage3}"
    WORKING_DIRECTORY "${VPP_ROOT}"
    RESULT_VARIABLE status OUTPUT_VARIABLE output ERROR_VARIABLE errors TIMEOUT 1200)
if (NOT status EQUAL 0 OR NOT output MATCHES "\"định dạng\":\"VPP bytecode\"")
    message(FATAL_ERROR "Stage 3 -> probe failed: ${status}\n${errors}\n${output}")
endif()
string(REGEX REPLACE "\r?\n$" "" output "${output}")
file(WRITE "${probe}" "${output}")
execute_process(
    COMMAND "${CMAKE_COMMAND}" -E env "VPP_HOME=${VPP_ROOT}"
        "${VPP_EXEC}" run --artifact "${probe}"
    WORKING_DIRECTORY "${VPP_ROOT}"
    RESULT_VARIABLE status OUTPUT_VARIABLE output ERROR_VARIABLE errors TIMEOUT 120)
if (NOT status EQUAL 0 OR NOT output STREQUAL "52\n")
    message(FATAL_ERROR "Stage 3 probe invalid: ${status}\n${errors}\n${output}")
endif()

message(STATUS "Stage 1 -> Stage 2 -> Stage 3 PASS; probe = 52; Stage 2 SHA256 ${stage2_sha}; Stage 3 SHA256 ${stage3_sha}")
