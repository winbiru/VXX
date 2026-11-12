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
execute_process(COMMAND "${executable}" RESULT_VARIABLE status
    OUTPUT_VARIABLE reference_output ERROR_VARIABLE errors)
if (NOT status EQUAL 42)
    message(FATAL_ERROR "self-host executable should exit 42: ${status} ${errors}")
endif()

# Once Stage 1 is available, the public CLI can build the same program via
# the native V++ compiler and ASM linker, without entering the Stage 0 VM.
if (DEFINED VPP_STAGE1 AND NOT "${VPP_STAGE1}" STREQUAL "")
    set(nativeVppExecutable "${VPP_OUTPUT_DIR}/native vpp executable")
    file(REMOVE "${nativeVppExecutable}")
    execute_process(
        COMMAND "${CMAKE_COMMAND}" -E env "VPP_HOME=${VPP_HOME}"
            "VPP_NATIVE_COMPILER=${VPP_STAGE1}"
            "VPP_NATIVE_CC=${VPP_OUTPUT_DIR}/missing host cc"
            "${VPP_EXEC}" build --target "${VPP_TARGET}" --emit-exe-vpp
            "${sourcecopy}" "${nativeVppExecutable}"
        WORKING_DIRECTORY "${VPP_OUTPUT_DIR}"
        RESULT_VARIABLE native_status OUTPUT_VARIABLE native_output ERROR_VARIABLE native_errors)
    if (NOT native_status EQUAL 0 OR NOT EXISTS "${nativeVppExecutable}")
        message(FATAL_ERROR "V++/ASM executable CLI failed: ${native_status} ${native_errors} ${native_output}")
    endif()
    execute_process(COMMAND "${nativeVppExecutable}"
        RESULT_VARIABLE native_status OUTPUT_VARIABLE native_output ERROR_VARIABLE native_errors)
    if (NOT native_status EQUAL status OR NOT native_output STREQUAL reference_output)
        message(FATAL_ERROR "V++/ASM CLI must match direct Stage 1 build: ${native_status} ${native_errors} ${native_output}")
    endif()
endif()

# Stage 0 uses the shared host process ABI for linking. An unavailable linker
# must surface its exit code and preserve an already published executable.
if (NOT DEFINED VPP_STAGE1 OR "${VPP_STAGE1}" STREQUAL "")
    file(SHA256 "${executable}" linked_hash)
    execute_process(
        COMMAND "${CMAKE_COMMAND}" -E env "VPP_HOME=${VPP_HOME}"
            "VPP_NATIVE_CC=${VPP_OUTPUT_DIR}/missing native linker"
            ${compile_command}
        WORKING_DIRECTORY "${VPP_OUTPUT_DIR}"
        RESULT_VARIABLE linker_status OUTPUT_VARIABLE linker_output ERROR_VARIABLE linker_errors)
    file(SHA256 "${executable}" preserved_hash)
    if (linker_status EQUAL 0 OR NOT linked_hash STREQUAL preserved_hash OR
        NOT linker_errors MATCHES "Native host linker thất bại \\(mã 127\\)")
        message(FATAL_ERROR "self-host linker failure must report 127 and preserve output: ${linker_status} ${linker_errors} ${linker_output}")
    endif()
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
# Both compilation paths support division. Use a malformed expression to
# exercise failed-build publication regardless of the available opcodes.
file(WRITE "${VPP_OUTPUT_DIR}/unsupported.vi"
    "hàm chính() { trả về ( ; };\n")
file(WRITE "${executable}" "existing-executable")
vpp_native_compile_command("${VPP_OUTPUT_DIR}/unsupported.vi"
    "${executable}" compile_command)
execute_process(
    COMMAND "${CMAKE_COMMAND}" -E env "VPP_HOME=${VPP_HOME}"
        ${compile_command}
    WORKING_DIRECTORY "${VPP_OUTPUT_DIR}"
    RESULT_VARIABLE status ERROR_VARIABLE errors)
file(READ "${executable}" content)
if (status EQUAL 0 OR NOT content STREQUAL "existing-executable")
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
