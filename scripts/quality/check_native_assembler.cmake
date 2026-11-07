# Verify compiler-emitted ASM is accepted by Clang's real assembler.
if (NOT DEFINED VPP_EXEC OR NOT DEFINED VPP_CLANG OR NOT DEFINED VPP_LEAF OR
    NOT DEFINED VPP_GENERAL OR NOT DEFINED VPP_OUTPUT_DIR)
    message(FATAL_ERROR "Native ASM: missing inputs")
endif()
file(MAKE_DIRECTORY "${VPP_OUTPUT_DIR}")
foreach(case IN ITEMS
    "i386-unknown-linux-gnu#${VPP_LEAF}#tra42"
    "x86_64-unknown-linux-gnu#${VPP_LEAF}#cong"
    "x86_64-unknown-linux-gnu#${VPP_LEAF}#tra_i32_am"
    "aarch64-unknown-linux-gnu#${VPP_LEAF}#tru"
    "aarch64-apple-darwin#${VPP_LEAF}#tra42"
    "aarch64-apple-darwin#${VPP_LEAF}#tra_i32_am"
    "aarch64-apple-darwin#${VPP_LEAF}#cong"
    "aarch64-apple-darwin#${VPP_LEAF}#tru"
    "aarch64-apple-darwin#${VPP_LEAF}#hang_chia"
    "x86_64-pc-windows-msvc#${VPP_LEAF}#cong"
    "aarch64-unknown-linux-gnu#${VPP_GENERAL}#tong"
    "x86_64-unknown-linux-gnu#${VPP_GENERAL}#tinh")
    string(REPLACE "#" ";" parts "${case}")
    list(GET parts 0 target)
    list(GET parts 1 source)
    list(GET parts 2 name)
    set(asm "${VPP_OUTPUT_DIR}/${target}-${name}.s")
    set(obj "${VPP_OUTPUT_DIR}/${target}-${name}.o")
    execute_process(COMMAND "${VPP_EXEC}" build --target "${target}" --emit-asm
        "${name}" "${source}" "${asm}" RESULT_VARIABLE status ERROR_VARIABLE errors TIMEOUT 90)
    if (NOT status EQUAL 0)
        message(FATAL_ERROR "V++ ASM emit failed for ${target}/${name}: ${errors}")
    endif()
    execute_process(COMMAND "${VPP_CLANG}" -target "${target}" -c "${asm}" -o "${obj}"
        RESULT_VARIABLE status ERROR_VARIABLE errors TIMEOUT 90)
    if (NOT status EQUAL 0 OR NOT EXISTS "${obj}")
        message(FATAL_ERROR "Clang rejected V++ ASM ${target}/${name}: ${errors}")
    endif()
endforeach()

# Check that the ARM64 leaf mnemonic printer agrees with the machine ABI,
# including signed i32 boundaries. Run the assembled V++ functions on host.
if (CMAKE_HOST_SYSTEM_NAME STREQUAL "Darwin" AND
    CMAKE_HOST_SYSTEM_PROCESSOR MATCHES "^(arm64|aarch64)$")
    set(leaf_prefix "${VPP_OUTPUT_DIR}/aarch64-apple-darwin")
    set(harness "${VPP_OUTPUT_DIR}/native-leaf-asm-smoke.c")
    set(executable "${VPP_OUTPUT_DIR}/native-leaf-asm-smoke")
    file(WRITE "${harness}" "#include <stdint.h>\n"
        "int tra42(void);\nint tra_i32_am(void);\n"
        "int cong(int, int);\nint tru(int, int);\nint hang_chia(void);\n"
        "int main(void) {\n"
        "  if (tra42() != 42 || tra_i32_am() != INT32_MIN) return 1;\n"
        "  if (cong(-7, 42) != 35 || cong(INT32_MIN, 0) != INT32_MIN) return 2;\n"
        "  if (tru(42, -7) != 49 || tru(INT32_MIN, 0) != INT32_MIN) return 3;\n"
        "  if (hang_chia() != 5) return 4;\n"
        "  return 0;\n}\n")
    execute_process(COMMAND "${VPP_CLANG}" -target aarch64-apple-darwin
        "${harness}" "${leaf_prefix}-tra42.o" "${leaf_prefix}-tra_i32_am.o"
        "${leaf_prefix}-cong.o" "${leaf_prefix}-tru.o"
        "${leaf_prefix}-hang_chia.o" -o "${executable}"
        RESULT_VARIABLE status ERROR_VARIABLE errors TIMEOUT 90)
    if (NOT status EQUAL 0)
        message(FATAL_ERROR "V++ ARM64 leaf ASM host link failed: ${errors}")
    endif()
    execute_process(COMMAND "${executable}"
        RESULT_VARIABLE status ERROR_VARIABLE errors TIMEOUT 30)
    if (NOT status EQUAL 0)
        message(FATAL_ERROR "V++ ARM64 leaf ASM runtime mismatch (status ${status}): ${errors}")
    endif()
endif()
message(STATUS "V++ Assembly accepted by Clang; ARM64 host leaf execution: PASS (where supported)")
