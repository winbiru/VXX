if (NOT DEFINED VPP_EXEC OR NOT DEFINED VPP_SOURCE OR
    NOT DEFINED VPP_TARGET OR NOT DEFINED VPP_OUTPUT_DIR OR NOT DEFINED VPP_HOME)
    message(FATAL_ERROR "missing self-host executable smoke arguments")
endif()

file(MAKE_DIRECTORY "${VPP_OUTPUT_DIR}")
include("${CMAKE_CURRENT_LIST_DIR}/native_stage1_compile.cmake")
set(sourcecopy "${VPP_OUTPUT_DIR}/nativeaotprogram.vi")
configure_file("${VPP_SOURCE}" "${sourcecopy}" COPYONLY)
# Verify the linker path also handles whitespace without shell interpretation.
set(executable "${VPP_OUTPUT_DIR}/native aot program")
file(REMOVE "${executable}")
vpp_native_compile_command("${sourcecopy}" "${executable}" compile_command)
execute_process(
    COMMAND "${CMAKE_COMMAND}" -E env "VPP_HOME=${VPP_HOME}"
        ${compile_command}
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
file(REMOVE "${manyExecutable}")
vpp_native_compile_command("${manySource}" "${manyExecutable}" compile_command)
execute_process(
    COMMAND "${CMAKE_COMMAND}" -E env "VPP_HOME=${VPP_HOME}"
        ${compile_command}
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
# Stage 1 already supports division, so use an undefined variable instead.
if (DEFINED VPP_STAGE1 AND NOT "${VPP_STAGE1}" STREQUAL "")
    file(WRITE "${VPP_OUTPUT_DIR}/unsupported.vi"
        "hàm chính() { trả về unknown; };\n")
else()
    file(WRITE "${VPP_OUTPUT_DIR}/unsupported.vi"
        "hàm main() { trả về 20 / 2; };\n")
endif()
file(WRITE "${executable}" "existing-executable")
vpp_native_compile_command("${VPP_OUTPUT_DIR}/unsupported.vi"
    "${executable}" compile_command)
execute_process(
    COMMAND "${CMAKE_COMMAND}" -E env "VPP_HOME=${VPP_HOME}"
        ${compile_command}
    WORKING_DIRECTORY "${VPP_OUTPUT_DIR}"
    RESULT_VARIABLE status ERROR_VARIABLE errors)
file(READ "${executable}" content)
if (status EQUAL 0 OR NOT content STREQUAL "existing-executable" OR
    (NOT DEFINED VPP_STAGE1 AND NOT errors MATCHES "chưa hỗ trợ"))
    message(FATAL_ERROR "self-host AOT must reject unsupported syntax without overwriting output: ${status} ${errors}")
endif()

file(WRITE "${VPP_OUTPUT_DIR}/missing-main.vi" "hàm foo() { trả về 12; };\n")
vpp_native_compile_command("${VPP_OUTPUT_DIR}/missing-main.vi"
    "${executable}" compile_command)
execute_process(
    COMMAND "${CMAKE_COMMAND}" -E env "VPP_HOME=${VPP_HOME}"
        ${compile_command}
    WORKING_DIRECTORY "${VPP_OUTPUT_DIR}"
    RESULT_VARIABLE status ERROR_VARIABLE errors)
file(READ "${executable}" content)
if (status EQUAL 0 OR NOT content STREQUAL "existing-executable" OR
    (NOT DEFINED VPP_STAGE1 AND NOT errors MATCHES "main"))
    message(FATAL_ERROR "self-host AOT must require main and preserve output: ${status} ${errors}")
endif()

file(WRITE "${VPP_OUTPUT_DIR}/duplicate-entry.vi"
    "hàm chính() { trả về 1; };\nhàm main() { trả về 2; };\n")
vpp_native_compile_command("${VPP_OUTPUT_DIR}/duplicate-entry.vi"
    "${executable}" compile_command)
execute_process(
    COMMAND "${CMAKE_COMMAND}" -E env "VPP_HOME=${VPP_HOME}"
        ${compile_command}
    WORKING_DIRECTORY "${VPP_OUTPUT_DIR}"
    RESULT_VARIABLE status ERROR_VARIABLE errors)
file(READ "${executable}" content)
if (status EQUAL 0 OR NOT content STREQUAL "existing-executable" OR
    (NOT DEFINED VPP_STAGE1 AND NOT errors MATCHES "đồng thời"))
    message(FATAL_ERROR "self-host AOT must reject duplicate entrypoints: ${status} ${errors}")
endif()

# An output symlink to the source must be rejected before compilation.
set(sourceAlias "${VPP_OUTPUT_DIR}/source-link")
file(REMOVE "${sourceAlias}")
file(CREATE_LINK "${sourcecopy}" "${sourceAlias}" SYMBOLIC)
file(READ "${sourcecopy}" before)
vpp_native_compile_command("${sourcecopy}" "${sourceAlias}" compile_command)
execute_process(
    COMMAND "${CMAKE_COMMAND}" -E env "VPP_HOME=${VPP_HOME}"
        ${compile_command}
    WORKING_DIRECTORY "${VPP_OUTPUT_DIR}"
    RESULT_VARIABLE status ERROR_VARIABLE errors)
file(READ "${sourcecopy}" after)
if (status EQUAL 0 OR NOT before STREQUAL after OR
    (NOT DEFINED VPP_STAGE1 AND NOT errors MATCHES "ghi đè mã nguồn"))
    message(FATAL_ERROR "self-host AOT must protect symlinked source: ${status} ${errors}")
endif()
