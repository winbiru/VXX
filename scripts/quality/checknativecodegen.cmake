# The ARM64 Machine IR and Mach-O object writer execute as compiled V++.
if (NOT DEFINED VPP_EXEC OR NOT DEFINED VPP_ROOT OR NOT DEFINED VPP_TARGET OR
    NOT DEFINED VPP_OUTPUT_DIR)
    message(FATAL_ERROR "missing native codegen test arguments")
endif()
file(MAKE_DIRECTORY "${VPP_OUTPUT_DIR}")
set(source "${VPP_OUTPUT_DIR}/nativecodegen.vi")
set(harness "${VPP_OUTPUT_DIR}/nativecodegen")
file(WRITE "${source}" "")
foreach(part IN ITEMS
    "gói/biên dịch/native/lexer.vi"
    "gói/biên dịch/native/tokens.vi"
    "gói/biên dịch/native/parser.vi"
    "gói/biên dịch/native/ir.vi"
    "gói/biên dịch/native/codegen.vi"
    "gói/biên dịch/native/object.vi"
    "src/tests/nativecodegenharness.vi")
    file(READ "${VPP_ROOT}/${part}" contents)
    file(APPEND "${source}" "${contents}\n")
endforeach()
file(WRITE "${VPP_OUTPUT_DIR}/native-codegen-input.vi"
    "hàm chính() { biến x = -159; x /= -3; x %= 52; x += 51; nếu (!sai) { trả về x; }; trả về 1; };\n")
file(REMOVE "${harness}")
if (DEFINED VPP_STAGE1 AND NOT "${VPP_STAGE1}" STREQUAL "")
    if (NOT EXISTS "${VPP_STAGE1}")
        message(FATAL_ERROR "native Stage 1 compiler missing: ${VPP_STAGE1}")
    endif()
    set(compile_command "${VPP_STAGE1}" build "${source}" -o "${harness}")
else()
    set(compile_command "${VPP_EXEC}" build --target "${VPP_TARGET}"
        --emit-exe-selfhost "${source}" "${harness}")
endif()
execute_process(COMMAND "${CMAKE_COMMAND}" -E env "VPP_HOME=${VPP_ROOT}"
    ${compile_command}
    WORKING_DIRECTORY "${VPP_OUTPUT_DIR}"
    RESULT_VARIABLE status OUTPUT_VARIABLE output ERROR_VARIABLE errors TIMEOUT 300)
if (NOT status EQUAL 0 OR NOT EXISTS "${harness}")
    message(FATAL_ERROR "native codegen test build failed: ${status}\n${output}\n${errors}")
endif()
execute_process(COMMAND "${harness}"
    WORKING_DIRECTORY "${VPP_OUTPUT_DIR}"
    RESULT_VARIABLE status OUTPUT_VARIABLE output ERROR_VARIABLE errors TIMEOUT 45)
if (NOT status EQUAL 0 OR NOT output STREQUAL "20261017\n")
    message(FATAL_ERROR "native codegen test run failed: ${status}\n${output}\n${errors}")
endif()
set(object "${VPP_OUTPUT_DIR}/native-codegen-output.o")
if (NOT EXISTS "${object}")
    message(FATAL_ERROR "native Mach-O object not written")
endif()
file(READ "${object}" header HEX LIMIT 4)
if (NOT header STREQUAL "cffaedfe")
    message(FATAL_ERROR "native writer did not emit a Mach-O 64-bit object: ${header}")
endif()
execute_process(COMMAND /usr/bin/clang -arch arm64 "${object}"
    -o "${VPP_OUTPUT_DIR}/native-codegen-result"
    RESULT_VARIABLE status OUTPUT_VARIABLE output ERROR_VARIABLE errors TIMEOUT 45)
if (NOT status EQUAL 0)
    message(FATAL_ERROR "native Mach-O link failed: ${status}\n${output}\n${errors}")
endif()
execute_process(COMMAND "${VPP_OUTPUT_DIR}/native-codegen-result"
    RESULT_VARIABLE status TIMEOUT 30)
if (NOT status EQUAL 52)
    message(FATAL_ERROR "native Mach-O executable should return 52, got ${status}")
endif()
message(STATUS "V++ ARM64 native codegen and Mach-O executable: PASS")
