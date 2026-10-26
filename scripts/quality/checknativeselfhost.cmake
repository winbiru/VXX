if (NOT DEFINED VPP_EXEC OR NOT DEFINED VPP_SOURCE OR
    NOT DEFINED VPP_CC OR NOT DEFINED VPP_TARGET OR
    NOT DEFINED VPP_OUTPUT_DIR OR NOT DEFINED VPP_SMOKE_SOURCE OR
    NOT DEFINED VPP_HOME)
    message(FATAL_ERROR "missing native self-host smoke arguments")
endif()

file(MAKE_DIRECTORY "${VPP_OUTPUT_DIR}")
# Copy the source outside the repository too: otherwise its parent folders
# would resolve `gói/` even when VPP_HOME lookup were broken.
set(sourcecopy "${VPP_OUTPUT_DIR}/nativeaotsmoke.vi")
configure_file("${VPP_SOURCE}" "${sourcecopy}" COPYONLY)
set(objects)
foreach(func IN ITEMS twice sum other)
    set(obj "${VPP_OUTPUT_DIR}/${func}.o")
    # Deliberately compile from outside the source tree: the V++ package
    # resolver must find the self-hosted compiler via VPP_HOME.
    execute_process(
        COMMAND "${CMAKE_COMMAND}" -E env "VPP_HOME=${VPP_HOME}"
            "${VPP_EXEC}" build --target "${VPP_TARGET}" --emit-obj-selfhost
            "${func}" "${sourcecopy}" "${obj}"
        WORKING_DIRECTORY "${VPP_OUTPUT_DIR}"
        RESULT_VARIABLE status ERROR_VARIABLE errors)
    if (NOT status EQUAL 0 OR NOT EXISTS "${obj}")
        message(FATAL_ERROR "V++ self-host object failed (${func}): ${errors}")
    endif()
    list(APPEND objects "${obj}")
endforeach()

set(executable "${VPP_OUTPUT_DIR}/nativeaotsmoke")
execute_process(COMMAND "${VPP_CC}" "${VPP_SMOKE_SOURCE}" ${objects} -o "${executable}"
    RESULT_VARIABLE status ERROR_VARIABLE errors)
if (NOT status EQUAL 0)
    message(FATAL_ERROR "V++ self-host host-link failed: ${errors}")
endif()
execute_process(COMMAND "${executable}" RESULT_VARIABLE status
    OUTPUT_VARIABLE output ERROR_VARIABLE errors)
if (NOT status EQUAL 0 OR NOT output MATCHES "native-selfhost-host-link-ok")
    message(FATAL_ERROR "V++ self-host execution failed: ${status} ${errors} ${output}")
endif()

# Exercise the AST -> V++ Machine IR -> native object path directly. Stage 1's
# separate executable compiler already tests division, so verify this path too.
set(arithmetic "${VPP_OUTPUT_DIR}/arithmetic.vi")
file(WRITE "${arithmetic}"
    "hàm arith(a, b) { trả về a / b + a % b; };\n"
    "hàm compound(a, b) { biến x = a; x /= b; x %= 52; trả về x; };\n")
set(arithmetic_objects)
foreach(func IN ITEMS arith compound)
    set(obj "${VPP_OUTPUT_DIR}/${func}.o")
    file(REMOVE "${obj}")
    execute_process(
        COMMAND "${CMAKE_COMMAND}" -E env "VPP_HOME=${VPP_HOME}"
            "${VPP_EXEC}" build --target "${VPP_TARGET}" --emit-obj-selfhost
            "${func}" "${arithmetic}" "${obj}"
        WORKING_DIRECTORY "${VPP_OUTPUT_DIR}"
        RESULT_VARIABLE status ERROR_VARIABLE errors)
    if (NOT status EQUAL 0 OR NOT EXISTS "${obj}")
        message(FATAL_ERROR "V++ native self-host arithmetic failed (${func}): ${errors}")
    endif()
    list(APPEND arithmetic_objects "${obj}")
endforeach()
set(driver "${VPP_OUTPUT_DIR}/arithmetic_smoke.c")
file(WRITE "${driver}" [=[
#include <stdint.h>
extern int32_t arith(int32_t, int32_t);
extern int32_t compound(int32_t, int32_t);
int main(void) {
    if (arith(-159, -3) != 53) return 1;
    if (arith(-159, 52) != -6) return 2;
    if (arith(10, -3) != -2) return 3;
    if (arith(INT32_MIN, -1) != INT32_MIN) return 4;
    if (arith(52, 0) != -2) return 5;
    if (compound(-159, -3) != 1) return 6;
    if (compound(INT32_MIN, -1) != -24) return 7;
    return 0;
}
]=])
set(arithmetic_exe "${VPP_OUTPUT_DIR}/arithmetic_smoke")
execute_process(COMMAND "${VPP_CC}" "${driver}" ${arithmetic_objects} -o "${arithmetic_exe}"
    RESULT_VARIABLE status ERROR_VARIABLE errors)
if (NOT status EQUAL 0)
    message(FATAL_ERROR "V++ self-host arithmetic host-link failed: ${errors}")
endif()
execute_process(COMMAND "${arithmetic_exe}" RESULT_VARIABLE status
    ERROR_VARIABLE errors)
if (NOT status EQUAL 0)
    message(FATAL_ERROR "V++ self-host arithmetic wrong result: ${status} ${errors}")
endif()

# Keep a negative semantic test: unsupported logical operations cannot silently
# be compiled as a different integer opcode.
file(WRITE "${VPP_OUTPUT_DIR}/unsupported.vi"
    "hàm bad(a) { trả về a && 1; };\n")
set(unwanted "${VPP_OUTPUT_DIR}/unsupported.o")
file(REMOVE "${unwanted}")
execute_process(
    COMMAND "${CMAKE_COMMAND}" -E env "VPP_HOME=${VPP_HOME}"
        "${VPP_EXEC}" build --target "${VPP_TARGET}" --emit-obj-selfhost
        bad "${VPP_OUTPUT_DIR}/unsupported.vi" "${unwanted}"
    WORKING_DIRECTORY "${VPP_OUTPUT_DIR}"
    RESULT_VARIABLE status ERROR_VARIABLE errors)
if (status EQUAL 0 OR EXISTS "${unwanted}" OR NOT errors MATCHES "chưa hỗ trợ")
    message(FATAL_ERROR "V++ native self-host must reject unsupported logical op: ${status} ${errors}")
endif()
