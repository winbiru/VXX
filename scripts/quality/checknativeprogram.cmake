if (NOT DEFINED VPP_EXEC OR NOT DEFINED VPP_SOURCE OR
    NOT DEFINED VPP_TARGET OR NOT DEFINED VPP_OUTPUT_DIR OR NOT DEFINED VPP_HOME)
    message(FATAL_ERROR "missing self-host executable smoke arguments")
endif()

file(MAKE_DIRECTORY "${VPP_OUTPUT_DIR}")
set(sourcecopy "${VPP_OUTPUT_DIR}/nativeaotprogram.vi")
configure_file("${VPP_SOURCE}" "${sourcecopy}" COPYONLY)
# Verify the linker path also handles whitespace without shell interpretation.
set(executable "${VPP_OUTPUT_DIR}/native aot program")
execute_process(
    COMMAND "${CMAKE_COMMAND}" -E env "VPP_HOME=${VPP_HOME}"
        "${VPP_EXEC}" build --target "${VPP_TARGET}"
        --emit-exe-selfhost "${sourcecopy}" "${executable}"
    WORKING_DIRECTORY "${VPP_OUTPUT_DIR}"
    RESULT_VARIABLE status OUTPUT_VARIABLE output ERROR_VARIABLE errors)
if (NOT status EQUAL 0 OR NOT EXISTS "${executable}")
    message(FATAL_ERROR "self-host executable build failed: ${status} ${errors} ${output}")
endif()
execute_process(COMMAND "${executable}" RESULT_VARIABLE status ERROR_VARIABLE errors)
if (NOT status EQUAL 42)
    message(FATAL_ERROR "self-host executable should exit 42: ${status} ${errors}")
endif()

# Exercise the full native compilation/link/run path with arguments well
# beyond the ABI's register parameters. All arguments must be preserved,
# including the last one, without imposing a small fixed arity limit.
set(manyParams "")
set(manyArgs "")
foreach(index RANGE 0 127)
    if (NOT index EQUAL 0)
        string(APPEND manyParams ", ")
        string(APPEND manyArgs ", ")
    endif()
    string(APPEND manyParams "p${index}")
    if (index EQUAL 127)
        string(APPEND manyArgs "52")
    else()
        string(APPEND manyArgs "0")
    endif()
endforeach()
set(manySource "${VPP_OUTPUT_DIR}/many-arguments.vi")
set(manyExecutable "${VPP_OUTPUT_DIR}/many-arguments")
file(WRITE "${manySource}"
    "hàm cuoi(${manyParams}) { trả về p127; };\nhàm chính() { trả về cuoi(${manyArgs}); };\n")
execute_process(
    COMMAND "${CMAKE_COMMAND}" -E env "VPP_HOME=${VPP_HOME}"
        "${VPP_EXEC}" build --target "${VPP_TARGET}"
        --emit-exe-selfhost "${manySource}" "${manyExecutable}"
    WORKING_DIRECTORY "${VPP_OUTPUT_DIR}"
    RESULT_VARIABLE status OUTPUT_VARIABLE output ERROR_VARIABLE errors)
if (NOT status EQUAL 0 OR NOT EXISTS "${manyExecutable}")
    message(FATAL_ERROR "128-argument self-host executable build failed: ${status} ${errors} ${output}")
endif()
execute_process(COMMAND "${manyExecutable}" RESULT_VARIABLE status ERROR_VARIABLE errors)
if (NOT status EQUAL 52)
    message(FATAL_ERROR "128-argument self-host executable should exit 52: ${status} ${errors}")
endif()

# A failure must preserve an existing executable (no partial publication).
file(WRITE "${VPP_OUTPUT_DIR}/unsupported.vi" "hàm main() { trả về 20 / 2; };\n")
file(WRITE "${executable}" "existing-executable")
execute_process(
    COMMAND "${CMAKE_COMMAND}" -E env "VPP_HOME=${VPP_HOME}"
        "${VPP_EXEC}" build --target "${VPP_TARGET}"
        --emit-exe-selfhost "${VPP_OUTPUT_DIR}/unsupported.vi" "${executable}"
    WORKING_DIRECTORY "${VPP_OUTPUT_DIR}"
    RESULT_VARIABLE status ERROR_VARIABLE errors)
file(READ "${executable}" content)
if (status EQUAL 0 OR NOT content STREQUAL "existing-executable" OR
    NOT errors MATCHES "chưa hỗ trợ")
    message(FATAL_ERROR "self-host AOT must reject unsupported syntax without overwriting output: ${status} ${errors}")
endif()

file(WRITE "${VPP_OUTPUT_DIR}/missing-main.vi" "hàm foo() { trả về 12; };\n")
execute_process(
    COMMAND "${CMAKE_COMMAND}" -E env "VPP_HOME=${VPP_HOME}"
        "${VPP_EXEC}" build --target "${VPP_TARGET}"
        --emit-exe-selfhost "${VPP_OUTPUT_DIR}/missing-main.vi" "${executable}"
    WORKING_DIRECTORY "${VPP_OUTPUT_DIR}"
    RESULT_VARIABLE status ERROR_VARIABLE errors)
file(READ "${executable}" content)
if (status EQUAL 0 OR NOT content STREQUAL "existing-executable" OR
    NOT errors MATCHES "main")
    message(FATAL_ERROR "self-host AOT must require main and preserve output: ${status} ${errors}")
endif()

file(WRITE "${VPP_OUTPUT_DIR}/duplicate-entry.vi"
    "hàm chính() { trả về 1; };\nhàm main() { trả về 2; };\n")
execute_process(
    COMMAND "${CMAKE_COMMAND}" -E env "VPP_HOME=${VPP_HOME}"
        "${VPP_EXEC}" build --target "${VPP_TARGET}"
        --emit-exe-selfhost "${VPP_OUTPUT_DIR}/duplicate-entry.vi" "${executable}"
    WORKING_DIRECTORY "${VPP_OUTPUT_DIR}"
    RESULT_VARIABLE status ERROR_VARIABLE errors)
file(READ "${executable}" content)
if (status EQUAL 0 OR NOT content STREQUAL "existing-executable" OR
    NOT errors MATCHES "đồng thời")
    message(FATAL_ERROR "self-host AOT must reject duplicate entrypoints: ${status} ${errors}")
endif()

# An output symlink to the source must be rejected before compilation.
set(sourceAlias "${VPP_OUTPUT_DIR}/source-link")
file(REMOVE "${sourceAlias}")
file(CREATE_LINK "${sourcecopy}" "${sourceAlias}" SYMBOLIC)
file(READ "${sourcecopy}" before)
execute_process(
    COMMAND "${CMAKE_COMMAND}" -E env "VPP_HOME=${VPP_HOME}"
        "${VPP_EXEC}" build --target "${VPP_TARGET}"
        --emit-exe-selfhost "${sourcecopy}" "${sourceAlias}"
    WORKING_DIRECTORY "${VPP_OUTPUT_DIR}"
    RESULT_VARIABLE status ERROR_VARIABLE errors)
file(READ "${sourcecopy}" after)
if (status EQUAL 0 OR NOT before STREQUAL after OR
    NOT errors MATCHES "ghi đè mã nguồn")
    message(FATAL_ERROR "self-host AOT must protect symlinked source: ${status} ${errors}")
endif()
