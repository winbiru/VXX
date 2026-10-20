# Compile V++ native lexer and a V++ test driver as one standalone module.
# The assembly uses no host-side lexer, parser, or pre-tokenized data.
if (NOT DEFINED VPP_EXEC OR NOT DEFINED VPP_KERNEL OR NOT DEFINED VPP_HARNESS OR
    NOT DEFINED VPP_TARGET OR NOT DEFINED VPP_OUTPUT_DIR OR NOT DEFINED VPP_HOME)
    message(FATAL_ERROR "missing native lexer test arguments")
endif()
file(MAKE_DIRECTORY "${VPP_OUTPUT_DIR}")
file(READ "${VPP_KERNEL}" kernel)
file(READ "${VPP_HARNESS}" driver)
set(source "${VPP_OUTPUT_DIR}/nativelexer.vi")
set(executable "${VPP_OUTPUT_DIR}/nativelexer")
file(WRITE "${source}" "${kernel}\n${driver}\n")
# CRLF, UTF-8 identifier, block comment and escaped string in an actual file.
file(WRITE "${VPP_OUTPUT_DIR}/native-lexer-input.vi"
    "// header\r\nhàm mẫu(a) { /* chú */ biến x = 12; in \"Chào\\n\"; trả về x; };\n")
file(REMOVE "${executable}")
if (DEFINED VPP_STAGE1 AND NOT "${VPP_STAGE1}" STREQUAL "")
    if (NOT EXISTS "${VPP_STAGE1}")
        message(FATAL_ERROR "native Stage 1 compiler missing: ${VPP_STAGE1}")
    endif()
    set(compile_command "${VPP_STAGE1}" build "${source}" -o "${executable}")
else()
    set(compile_command "${VPP_EXEC}" build --target "${VPP_TARGET}"
        --emit-exe-selfhost "${source}" "${executable}")
endif()
execute_process(COMMAND "${CMAKE_COMMAND}" -E env "VPP_HOME=${VPP_HOME}"
    ${compile_command}
    WORKING_DIRECTORY "${VPP_OUTPUT_DIR}"
    RESULT_VARIABLE compile_status OUTPUT_VARIABLE compile_output
    ERROR_VARIABLE compile_errors TIMEOUT 180)
if (NOT compile_status EQUAL 0 OR NOT EXISTS "${executable}")
    message(FATAL_ERROR "V++ native lexer compile failed: ${compile_status}\n${compile_output}\n${compile_errors}")
endif()
execute_process(COMMAND "${executable}"
    WORKING_DIRECTORY "${VPP_OUTPUT_DIR}"
    RESULT_VARIABLE run_status OUTPUT_VARIABLE output ERROR_VARIABLE errors TIMEOUT 30)
if (NOT run_status EQUAL 0 OR NOT output STREQUAL "20261017\n")
    message(FATAL_ERROR "V++ native lexer execution failed: ${run_status}\n${errors}\n${output}")
endif()
message(STATUS "V++ native UTF-8 lexer kernel: PASS")
