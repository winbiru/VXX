# CLI regression checks driven by CMake with .vi fixtures.
if (NOT DEFINED VPP_EXEC OR NOT DEFINED VPP_ROOT OR NOT DEFINED VPP_CASE OR
    NOT DEFINED VPP_OUTPUT_DIR)
    message(FATAL_ERROR "V++ CLI check: missing arguments")
endif()
file(MAKE_DIRECTORY "${VPP_OUTPUT_DIR}")
set(VPP_CHECK_TIMEOUT 240)

function(vpp_check expected)
    execute_process(COMMAND "${CMAKE_COMMAND}" -E env "VPP_HOME=${VPP_ROOT}"
        "${VPP_EXEC}" ${ARGN}
        WORKING_DIRECTORY "${VPP_OUTPUT_DIR}"
        RESULT_VARIABLE status OUTPUT_VARIABLE output ERROR_VARIABLE errors TIMEOUT ${VPP_CHECK_TIMEOUT})
    if (NOT status EQUAL 0 OR NOT output STREQUAL expected)
        message(FATAL_ERROR "V++ CLI failed (${status}): ${ARGN}\n${errors}\nOutput: ${output}\nExpected: ${expected}")
    endif()
endfunction()

if (VPP_CASE STREQUAL "compiler")
    set(fixtures "${VPP_ROOT}/src/tests/fixtures/checkcompilervpp")
    file(MAKE_DIRECTORY "${VPP_OUTPUT_DIR}/lib")
    file(COPY "${fixtures}/lib/math.vi" DESTINATION "${VPP_OUTPUT_DIR}/lib")
    file(COPY "${fixtures}/main.vi" DESTINATION "${VPP_OUTPUT_DIR}")
    set(main "${VPP_OUTPUT_DIR}/main.vi")
    set(expected "init\n42\n7\n7\n2\ntiếng Việt\nđúng\n")
    vpp_check("${expected}" run --compiler=cpp "${main}")
    vpp_check("${expected}" run --compiler=vpp "${main}")
    set(artifact "${VPP_OUTPUT_DIR}/app.vbc")
    execute_process(COMMAND "${VPP_EXEC}" build --compiler=vpp "${main}" -o "${artifact}"
        WORKING_DIRECTORY "${VPP_OUTPUT_DIR}"
        RESULT_VARIABLE status ERROR_VARIABLE errors TIMEOUT 240)
    if (NOT status EQUAL 0 OR NOT EXISTS "${artifact}")
        message(FATAL_ERROR "V++ artifact build failed: ${errors}")
    endif()
    file(REMOVE "${main}" "${VPP_OUTPUT_DIR}/lib/math.vi")
    vpp_check("${expected}" run --artifact "${artifact}")
    file(COPY "${fixtures}/unsupported_closure.vi" DESTINATION "${VPP_OUTPUT_DIR}")
    execute_process(COMMAND "${VPP_EXEC}" build --compiler=vpp
        "${VPP_OUTPUT_DIR}/unsupported_closure.vi" -o "${artifact}"
        RESULT_VARIABLE status TIMEOUT 240)
    if (status EQUAL 0)
        message(FATAL_ERROR "V++ compiler accepted unsupported closure")
    endif()
    vpp_check("${expected}" run --artifact "${artifact}")
    # Exercise boundary arithmetic through both source compilers, and then run
    # the V++-generated artifact after its source has been removed.
    file(COPY "${fixtures}/i32.vi" DESTINATION "${VPP_OUTPUT_DIR}")
    set(i32_source "${VPP_OUTPUT_DIR}/i32.vi")
    set(i32_expected "-2147483648\n2147483647\n1\n-2147483648\n2147483646\n2147483647\n2147483647\n-2147483647\n-2147483648\n-2147483648\n2147483647\n1\n-2147483648\n-2147483648\n")
    vpp_check("${i32_expected}" run --compiler=cpp "${i32_source}")
    vpp_check("${i32_expected}" run --compiler=vpp "${i32_source}")
    set(i32_artifact "${VPP_OUTPUT_DIR}/i32.vbc")
    execute_process(COMMAND "${CMAKE_COMMAND}" -E env "VPP_HOME=${VPP_ROOT}"
        "${VPP_EXEC}" build --compiler=vpp "${i32_source}" -o "${i32_artifact}"
        WORKING_DIRECTORY "${VPP_OUTPUT_DIR}"
        RESULT_VARIABLE status OUTPUT_VARIABLE output ERROR_VARIABLE errors TIMEOUT 240)
    if (NOT status EQUAL 0)
        message(FATAL_ERROR "V++ i32 artifact build failed (${status}):\n${errors}\n${output}")
    endif()
    file(REMOVE "${i32_source}")
    vpp_check("${i32_expected}" run --artifact "${i32_artifact}")
    message(STATUS "V++ compiler CLI and artifact preservation: PASS")
elseif (VPP_CASE STREQUAL "selfhost")
    set(VPP_CHECK_TIMEOUT 600)
    set(source "${VPP_ROOT}/src/tests/kiem_tra_selfhost_i32_artifact.vi")
    set(artifact "${VPP_OUTPUT_DIR}/selfhost-i32-artifact.vbc")
    set(expected "selfhost_i32_artifact_ok\n")
    vpp_check("${expected}" run --compiler=cpp "${source}")
    execute_process(COMMAND "${CMAKE_COMMAND}" -E env "VPP_HOME=${VPP_ROOT}"
        "${VPP_EXEC}" build --compiler=vpp "${source}" -o "${artifact}"
        WORKING_DIRECTORY "${VPP_OUTPUT_DIR}"
        RESULT_VARIABLE status OUTPUT_VARIABLE output ERROR_VARIABLE errors TIMEOUT 600)
    if (NOT status EQUAL 0)
        message(FATAL_ERROR "Self-host hardening artifact build failed (${status}):\n${errors}\n${output}")
    endif()
    vpp_check("${expected}" run --artifact "${artifact}")
    message(STATUS "V++-generated compiler validates ABI and compiles i32 source: PASS")
elseif (VPP_CASE STREQUAL "oop")
    set(fixtures "${VPP_ROOT}/src/tests/fixtures/check_oop_pipeline")
    file(COPY "${VPP_ROOT}/src/tests/fixtures/oop_pipeline/base.vi" DESTINATION "${VPP_OUTPUT_DIR}")
    file(COPY "${fixtures}/api.vi" "${fixtures}/inheritance.vi" DESTINATION "${VPP_OUTPUT_DIR}")
    set(source "${VPP_OUTPUT_DIR}/inheritance.vi")
    set(expected "10\n11\n21\n")
    vpp_check("${expected}" run --compiler=cpp "${source}")
    vpp_check("${expected}" run --compiler=vpp "${source}")
    set(artifact "${VPP_OUTPUT_DIR}/objects.vbc")
    execute_process(COMMAND "${VPP_EXEC}" build --compiler=vpp "${source}" -o "${artifact}"
        WORKING_DIRECTORY "${VPP_OUTPUT_DIR}"
        RESULT_VARIABLE status ERROR_VARIABLE errors TIMEOUT 240)
    if (NOT status EQUAL 0 OR NOT EXISTS "${artifact}")
        message(FATAL_ERROR "OOP artifact build failed: ${errors}")
    endif()
    file(REMOVE "${source}")
    vpp_check("${expected}" run --artifact "${artifact}")
    file(COPY "${fixtures}/missing_interface.vi" DESTINATION "${VPP_OUTPUT_DIR}")
    execute_process(COMMAND "${VPP_EXEC}" build --compiler=vpp
        "${VPP_OUTPUT_DIR}/missing_interface.vi" -o "${artifact}"
        RESULT_VARIABLE status TIMEOUT 240)
    if (status EQUAL 0)
        message(FATAL_ERROR "V++ accepted missing interface")
    endif()
    vpp_check("${expected}" run --artifact "${artifact}")
    file(COPY "${fixtures}/stdlib_collection.vi" DESTINATION "${VPP_OUTPUT_DIR}")
    vpp_check("42\n1\n2\n3\n" run --compiler=vpp "${VPP_OUTPUT_DIR}/stdlib_collection.vi")
    message(STATUS "V++ OOP imported interfaces, inheritance and artifact: PASS")
else()
    message(FATAL_ERROR "Unknown V++ CLI test case: ${VPP_CASE}")
endif()
