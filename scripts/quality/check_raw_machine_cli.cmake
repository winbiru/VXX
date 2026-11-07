if (NOT DEFINED VPP_EXEC OR NOT DEFINED VPP_SOURCE OR NOT DEFINED VPP_OUTPUT_DIR)
    message(FATAL_ERROR "missing executable, source or temporary output directory")
endif()

file(MAKE_DIRECTORY "${VPP_OUTPUT_DIR}")
set(raw_file "${VPP_OUTPUT_DIR}/raw-leaf-test.bin")

# Raw native compilation still passes through the direct-IR bytecode pipeline.
# Verify that grouped unary expressions produce correct runnable bytecode,
# including unary plus, logical not, and negation of a parameter expression.
get_filename_component(source_dir "${VPP_SOURCE}" DIRECTORY)
execute_process(
    COMMAND "${VPP_EXEC}" chạy "${source_dir}/kiem_tra_native_unary.vi"
    RESULT_VARIABLE unary_result OUTPUT_VARIABLE unary_output ERROR_VARIABLE unary_error)
if (NOT unary_result EQUAL 0 OR
    NOT unary_output STREQUAL "-7\n7\n-2\n7\n-7\n-7\n1\n-2147483648\n-2147483648\n7\n-3.0\n")
    message(FATAL_ERROR "unary direct-IR VM regression: ${unary_error}; output=${unary_output}")
endif()

# Overflow must be rejected at compile time, with the offending value,
# instead of an internal stoi error or a silently truncated machine integer.
foreach(out_of_range IN ITEMS "2147483648" "-2147483649" "-9223372036854775809")
    file(WRITE "${VPP_OUTPUT_DIR}/out_of_range.vi"
        "hàm chính() { in ${out_of_range}; }\n")
    execute_process(
        COMMAND "${VPP_EXEC}" chạy "${VPP_OUTPUT_DIR}/out_of_range.vi"
        RESULT_VARIABLE overflow_result ERROR_VARIABLE overflow_error
        OUTPUT_VARIABLE overflow_output)
    # Native V++ reports a failed i32 conversion, while Stage 0 includes
    # the literal-specific overflow diagnostic. Both must reject the input.
    if (overflow_result EQUAL 0 OR
        NOT overflow_error MATCHES "Số nguyên vượt phạm vi i32|thành số nguyên: giá trị không thể chuyển đổi")
        message(FATAL_ERROR "integer literal overflow not rejected (${out_of_range}): ${overflow_error}; ${overflow_output}")
    endif()
endforeach()

execute_process(
    COMMAND "${VPP_EXEC}" build --target riscv64-linux-gnu --emit-raw
        tra42 "${VPP_SOURCE}" "${raw_file}"
    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
if (NOT result EQUAL 0)
    message(FATAL_ERROR "cannot compile V++ IR to RISC-V machine bytes: ${error}")
endif()
file(READ "${raw_file}" bytes HEX)
if (NOT bytes STREQUAL "370500001b05a50267800000")
    message(FATAL_ERROR "RISC-V raw machine code wrong: ${bytes}")
endif()

execute_process(
    COMMAND "${VPP_EXEC}" build --target riscv64-linux-gnu --emit-raw
        tinh_hang "${VPP_SOURCE}" "${raw_file}"
    RESULT_VARIABLE result ERROR_VARIABLE error)
if (NOT result EQUAL 0)
    message(FATAL_ERROR "cannot fold nested V++ arithmetic to native code: ${error}")
endif()
file(READ "${raw_file}" bytes HEX)
if (NOT bytes STREQUAL "370500001b05050367800000")
    message(FATAL_ERROR "nested V++ arithmetic must return 48: ${bytes}")
endif()

execute_process(
    COMMAND "${VPP_EXEC}" build --target x86_64-linux-gnu --emit-raw
        cong_dao "${VPP_SOURCE}" "${raw_file}"
    RESULT_VARIABLE result ERROR_VARIABLE error)
if (NOT result EQUAL 0)
    message(FATAL_ERROR "cannot lower commutative b+a: ${error}")
endif()
file(READ "${raw_file}" bytes HEX)
if (NOT bytes STREQUAL "89f801f0c3")
    message(FATAL_ERROR "b+a must use x86-64 native add: ${bytes}")
endif()

set(asm_file "${VPP_OUTPUT_DIR}/raw-leaf-test.s")
execute_process(
    COMMAND "${VPP_EXEC}" build --target riscv64-linux-gnu --emit-asm
        tra42 "${VPP_SOURCE}" "${asm_file}"
    RESULT_VARIABLE result ERROR_VARIABLE error)
if (NOT result EQUAL 0)
    message(FATAL_ERROR "cannot emit RISC-V assembly: ${error}")
endif()
file(READ "${asm_file}" listing)
if (NOT listing MATCHES "lui a0, 0" OR
    NOT listing MATCHES "addiw a0, a0, 42" OR
    NOT listing MATCHES "jalr zero, ra, 0")
    message(FATAL_ERROR "RISC-V assembly listing missing expected instructions: ${listing}")
endif()

execute_process(
    COMMAND "${VPP_EXEC}" build --target s390x-linux-gnu --emit-raw
        cong "${VPP_SOURCE}" "${raw_file}"
    RESULT_VARIABLE result ERROR_VARIABLE error)
if (NOT result EQUAL 0)
    message(FATAL_ERROR "cannot compile V++ IR to s390x add: ${error}")
endif()
file(READ "${raw_file}" bytes HEX)
if (NOT bytes STREQUAL "1a2307fe")
    message(FATAL_ERROR "s390x add bytes wrong: ${bytes}")
endif()

execute_process(
    COMMAND "${VPP_EXEC}" build --target s390x-linux-gnu --emit-raw
        tru "${VPP_SOURCE}" "${raw_file}"
    RESULT_VARIABLE result ERROR_VARIABLE error)
if (NOT result EQUAL 0)
    message(FATAL_ERROR "cannot compile V++ IR to s390x subtract: ${error}")
endif()
file(READ "${raw_file}" bytes HEX)
if (NOT bytes STREQUAL "1b2307fe")
    message(FATAL_ERROR "s390x subtract bytes wrong: ${bytes}")
endif()

execute_process(
    COMMAND "${VPP_EXEC}" build --target x86_64-windows-msvc --emit-raw
        tru "${VPP_SOURCE}" "${raw_file}"
    RESULT_VARIABLE result ERROR_VARIABLE error)
if (NOT result EQUAL 0)
    message(FATAL_ERROR "cannot compile V++ IR to Win64 subtract: ${error}")
endif()
file(READ "${raw_file}" bytes HEX)
if (NOT bytes STREQUAL "89c829d0c3")
    message(FATAL_ERROR "Win64 subtraction ABI bytes wrong: ${bytes}")
endif()

execute_process(
    COMMAND "${VPP_EXEC}" build --target aarch64-apple-darwin --emit-asm
        tru "${VPP_SOURCE}" "${asm_file}"
    RESULT_VARIABLE result ERROR_VARIABLE error)
if (NOT result EQUAL 0)
    message(FATAL_ERROR "cannot emit ARM64 subtract assembly: ${error}")
endif()
file(READ "${asm_file}" listing)
if (NOT listing MATCHES "sub w0, w0, w1" OR
    NOT listing MATCHES "ret")
    message(FATAL_ERROR "ARM64 subtraction listing wrong: ${listing}")
endif()

execute_process(
    COMMAND "${VPP_EXEC}" build --target s390x-linux-gnu --emit-raw
        tra_i32 "${VPP_SOURCE}" "${raw_file}"
    RESULT_VARIABLE result ERROR_VARIABLE error)
if (NOT result EQUAL 0)
    message(FATAL_ERROR "cannot compile s390x signed-i32 constant: ${error}")
endif()
file(READ "${raw_file}" bytes HEX)
if (NOT bytes STREQUAL "c0217fffffff07fe")
    message(FATAL_ERROR "s390x LGFI machine code wrong: ${bytes}")
endif()

foreach(negative_min IN ITEMS tra_i32_am tra_i32_am_ngoac)
    execute_process(
        COMMAND "${VPP_EXEC}" build --target s390x-linux-gnu --emit-raw
            "${negative_min}" "${VPP_SOURCE}" "${raw_file}"
        RESULT_VARIABLE result ERROR_VARIABLE error)
    if (NOT result EQUAL 0)
        message(FATAL_ERROR "cannot compile i32 minimum (${negative_min}): ${error}")
    endif()
    file(READ "${raw_file}" bytes HEX)
    if (NOT bytes STREQUAL "c0218000000007fe")
        message(FATAL_ERROR "s390x negative-i32-minimum encoding wrong: ${bytes}")
    endif()
endforeach()

file(REMOVE "${raw_file}")
file(REMOVE "${asm_file}")
foreach(unsupported IN ITEMS hang_tran hang_tran_trung_gian)
    set(expected_diagnostic "biểu thức hằng native vượt phạm vi i32 hoặc tràn trung gian")
    execute_process(
        COMMAND "${VPP_EXEC}" build --target aarch64-apple-darwin --emit-raw
            "${unsupported}" "${VPP_SOURCE}" "${raw_file}"
        RESULT_VARIABLE result ERROR_VARIABLE error)
    if (result EQUAL 0 OR NOT error MATCHES "${expected_diagnostic}")
        message(FATAL_ERROR "unsafe arithmetic must fail lowering (${unsupported}): ${error}")
    endif()
    if (EXISTS "${raw_file}")
        message(FATAL_ERROR "unsafe arithmetic must not write output: ${unsupported}")
    endif()
endforeach()
# Phép chia hằng đã được Machine IR i32 hỗ trợ; bảo đảm CLI xuất được
# machine bytes thực tế, thay vì kiểm tra diagnostic lỗi thời của leaf lowerer.
execute_process(
    COMMAND "${VPP_EXEC}" build --target aarch64-apple-darwin --emit-raw
        hang_chia "${VPP_SOURCE}" "${raw_file}"
    RESULT_VARIABLE result ERROR_VARIABLE error)
if (NOT result EQUAL 0 OR NOT EXISTS "${raw_file}")
    message(FATAL_ERROR "native i32 division lowering must emit bytes: ${error}")
endif()
file(SIZE "${raw_file}" div_size)
math(EXPR div_remainder "${div_size} % 4")
if (div_size LESS 4 OR NOT div_remainder EQUAL 0)
    message(FATAL_ERROR "native AArch64 i32 division invalid code size: ${div_size}")
endif()
file(REMOVE "${raw_file}")
execute_process(
    COMMAND "${VPP_EXEC}" build --target aarch64-apple-darwin --emit-raw
        tru_dao "${VPP_SOURCE}" "${raw_file}"
    RESULT_VARIABLE result ERROR_VARIABLE error)
if (NOT result EQUAL 0)
    message(FATAL_ERROR "general machine IR must support reversed subtraction: ${error}")
endif()
if (NOT EXISTS "${raw_file}")
    message(FATAL_ERROR "general machine IR must emit reversed subtraction")
endif()
file(REMOVE "${raw_file}")
execute_process(
    COMMAND "${VPP_EXEC}" build --target aarch64-apple-darwin --emit-raw
        chua_ho_tro "${VPP_SOURCE}" "${raw_file}"
    RESULT_VARIABLE result ERROR_VARIABLE error)
if (result EQUAL 0 OR NOT error MATCHES "Raw machine code không thể chứa CALL chưa liên kết")
    message(FATAL_ERROR "must reject effects outside leaf subset: ${error}")
endif()
if (EXISTS "${raw_file}")
    message(FATAL_ERROR "unsupported native IR must not write output")
endif()
