# Compile the same multi-module graph with serial and native V++ workers.
# Both paths must produce the same artifact and execute identically.
foreach(required IN ITEMS VPP_EXEC VPP_ROOT VPP_NATIVE_COMPILER VPP_OUTPUT_DIR)
    if (NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "Compiler workers: missing ${required}")
    endif()
endforeach()
if (NOT EXISTS "${VPP_NATIVE_COMPILER}")
    message(FATAL_ERROR "Compiler workers: native compiler missing: ${VPP_NATIVE_COMPILER}")
endif()

file(MAKE_DIRECTORY "${VPP_OUTPUT_DIR}/lib")
file(WRITE "${VPP_OUTPUT_DIR}/main.vi"
    "nhập lib.feature.* như f; nhập lib.math.* như m; hàm chính() { in f.tính(); in m.cộng(2, 3); };\n")
file(WRITE "${VPP_OUTPUT_DIR}/lib/feature.vi"
    "nhập math như m; hàm tính() { trả về m.cộng(19, 23); };\n")
file(WRITE "${VPP_OUTPUT_DIR}/lib/math.vi"
    "hàm cộng(a, b) { trả về a + b; };\n")

set(main "${VPP_OUTPUT_DIR}/main.vi")
foreach(workers IN ITEMS 0 1 2 4 8)
    set(out "${VPP_OUTPUT_DIR}/workers-${workers}.vbc")
    execute_process(
        COMMAND "${CMAKE_COMMAND}" -E env
            "VPP_HOME=${VPP_ROOT}" "VPP_AOT_ROOT=${VPP_ROOT}"
            "VPP_NATIVE_COMPILER=${VPP_NATIVE_COMPILER}"
            "VPP_COMPILER_WORKERS=${workers}"
            "${VPP_EXEC}" build --compiler=vpp "${main}" -o "${out}"
        RESULT_VARIABLE status ERROR_VARIABLE errors OUTPUT_VARIABLE output
        TIMEOUT 240)
    if (NOT status STREQUAL "0" OR NOT EXISTS "${out}")
        message(FATAL_ERROR "Compiler workers=${workers} failed (${status}): ${errors}\n${output}")
    endif()
    if (NOT workers EQUAL 0)
        execute_process(COMMAND "${CMAKE_COMMAND}" -E compare_files
            "${VPP_OUTPUT_DIR}/workers-0.vbc" "${out}"
            RESULT_VARIABLE identical)
        if (NOT identical EQUAL 0)
            message(FATAL_ERROR "Compiler workers=${workers} changed bytecode or StringPool order")
        endif()
    endif()
endforeach()

# One imported module is sufficient to run the root and dependency in
# different workers. This used to silently take the serial path.
file(WRITE "${VPP_OUTPUT_DIR}/one-import.vi"
    "nhập lib.math.* như m; hàm chính() { in m.cộng(40, 2); };\n")
foreach(workers IN ITEMS 0 2)
    set(out "${VPP_OUTPUT_DIR}/one-import-${workers}.vbc")
    execute_process(
        COMMAND "${CMAKE_COMMAND}" -E env
            "VPP_HOME=${VPP_ROOT}" "VPP_AOT_ROOT=${VPP_ROOT}"
            "VPP_NATIVE_COMPILER=${VPP_NATIVE_COMPILER}"
            "VPP_COMPILER_WORKERS=${workers}"
            "${VPP_EXEC}" build --compiler=vpp
            "${VPP_OUTPUT_DIR}/one-import.vi" -o "${out}"
        RESULT_VARIABLE status ERROR_VARIABLE errors TIMEOUT 240)
    if (NOT status STREQUAL "0" OR NOT EXISTS "${out}")
        message(FATAL_ERROR "Single import workers=${workers} failed (${status}): ${errors}")
    endif()
endforeach()
execute_process(COMMAND "${CMAKE_COMMAND}" -E compare_files
    "${VPP_OUTPUT_DIR}/one-import-0.vbc" "${VPP_OUTPUT_DIR}/one-import-2.vbc"
    RESULT_VARIABLE single_import_identical)
if (NOT single_import_identical EQUAL 0)
    message(FATAL_ERROR "Single import worker changed bytecode or StringPool order")
endif()
execute_process(
    COMMAND "${CMAKE_COMMAND}" -E env "VPP_HOME=${VPP_ROOT}"
        "${VPP_EXEC}" run --artifact "${VPP_OUTPUT_DIR}/one-import-2.vbc"
    RESULT_VARIABLE single_import_status OUTPUT_VARIABLE single_import_output
    ERROR_VARIABLE single_import_errors TIMEOUT 120)
if (NOT single_import_status STREQUAL "0" OR
    NOT single_import_output STREQUAL "42\n")
    message(FATAL_ERROR "Single import worker artifact failed (${single_import_status}): ${single_import_errors}\n${single_import_output}")
endif()

# Invalid configuration must be rejected by the self-hosted compiler itself.
foreach(invalid IN ITEMS không_hợp_lệ -1 9)
    execute_process(
        COMMAND "${CMAKE_COMMAND}" -E env
            "VPP_HOME=${VPP_ROOT}" "VPP_AOT_ROOT=${VPP_ROOT}"
            "VPP_NATIVE_COMPILER=${VPP_NATIVE_COMPILER}"
            "VPP_COMPILER_WORKERS=${invalid}"
            "${VPP_EXEC}" build --compiler=vpp "${main}"
        RESULT_VARIABLE invalid_status ERROR_VARIABLE invalid_error TIMEOUT 120)
    if (invalid_status STREQUAL "0" OR
        NOT invalid_error MATCHES "VPP_COMPILER_WORKERS")
        message(FATAL_ERROR "Compiler workers ignored ${invalid}: ${invalid_status} ${invalid_error}")
    endif()
endforeach()

execute_process(
    COMMAND "${CMAKE_COMMAND}" -E env "VPP_HOME=${VPP_ROOT}"
        "${VPP_EXEC}" run --artifact "${VPP_OUTPUT_DIR}/workers-2.vbc"
    RESULT_VARIABLE status OUTPUT_VARIABLE output ERROR_VARIABLE errors TIMEOUT 120)
if (NOT status STREQUAL "0" OR NOT output STREQUAL "42\n5\n")
    message(FATAL_ERROR "Compiler worker artifact failed (${status}): ${errors}\n${output}")
endif()
message(STATUS "Compiler workers parity (0/1/2/4/8 and single import) PASS")
