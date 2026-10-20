# Ghép các module nguồn V++ để chạy native khi native import chưa sẵn sàng.
if (NOT DEFINED VPP_EXEC OR NOT DEFINED VPP_ROOT OR NOT DEFINED VPP_TARGET OR
    NOT DEFINED VPP_OUTPUT_DIR)
    message(FATAL_ERROR "missing native parser test arguments")
endif()
file(MAKE_DIRECTORY "${VPP_OUTPUT_DIR}")
set(source "${VPP_OUTPUT_DIR}/nativeparser.vi")
set(executable "${VPP_OUTPUT_DIR}/nativeparser")
file(WRITE "${source}" "")
foreach(part IN ITEMS
    "gói/biên dịch/native/lexer.vi"
    "gói/biên dịch/native/tokens.vi"
    "gói/biên dịch/native/parser.vi"
    "gói/biên dịch/native/ir.vi"
    "src/tests/nativeparserharness.vi")
    file(READ "${VPP_ROOT}/${part}" contents)
    file(APPEND "${source}" "${contents}\n")
endforeach()
file(WRITE "${VPP_OUTPUT_DIR}/native-parser-input.vi"
    "// ví dụ\r\nhàm chính() { biến a = 12; trả về a * 4 + 4; };\n")
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
execute_process(COMMAND "${CMAKE_COMMAND}" -E env "VPP_HOME=${VPP_ROOT}"
    ${compile_command}
    WORKING_DIRECTORY "${VPP_OUTPUT_DIR}"
    RESULT_VARIABLE status OUTPUT_VARIABLE output ERROR_VARIABLE errors TIMEOUT 300)
if (NOT status EQUAL 0 OR NOT EXISTS "${executable}")
    message(FATAL_ERROR "native V++ token/parser/IR compile failed: ${status}\n${output}\n${errors}")
endif()
execute_process(COMMAND "${executable}"
    WORKING_DIRECTORY "${VPP_OUTPUT_DIR}"
    RESULT_VARIABLE status OUTPUT_VARIABLE output ERROR_VARIABLE errors TIMEOUT 45)
if (NOT status EQUAL 0 OR NOT output STREQUAL "20261017\n")
    message(FATAL_ERROR "native V++ token/parser/IR run failed: ${status}\n${output}\n${errors}")
endif()
if (NOT EXISTS "${VPP_OUTPUT_DIR}/native-parser-output.ir")
    message(FATAL_ERROR "native compiler frontend did not write its IR artifact")
endif()
file(SIZE "${VPP_OUTPUT_DIR}/native-parser-output.ir" ir_bytes)
file(READ "${VPP_OUTPUT_DIR}/native-parser-output.ir" ir_hex HEX)
# IR oracle độc lập cho nguồn: biến a=12; trả về a*4+4.
# PUSH 12, STORE 0, LOAD 0, PUSH 4, MUL, PUSH 4, ADD, RET.
string(CONCAT ir_expected
    "010000000c000000" "0300000000000000" "0200000000000000"
    "0100000004000000" "0600000000000000" "0100000004000000"
    "0400000000000000" "0700000000000000")
if (NOT ir_bytes EQUAL 64 OR NOT ir_hex STREQUAL ir_expected)
    message(FATAL_ERROR "native IR artifact differs from i32 wire oracle: ${ir_bytes} bytes, ${ir_hex}")
endif()

# Negative cases use the same native compiler as the positive parser harness.
# Stage 0 emits detailed Vietnamese diagnostics; Stage 1 currently reports
# parse errors by exit status, so both must reject without publishing output.
function(expect_native_rejection name code diagnostic)
    set(input "${VPP_OUTPUT_DIR}/${name}.vi")
    set(exe "${VPP_OUTPUT_DIR}/${name}")
    file(WRITE "${input}" "${code}\n")
    file(REMOVE "${exe}")
    if (DEFINED VPP_STAGE1 AND NOT "${VPP_STAGE1}" STREQUAL "")
        set(negative_command "${VPP_STAGE1}" build "${input}" -o "${exe}")
    else()
        set(negative_command "${VPP_EXEC}" build --target "${VPP_TARGET}"
            --emit-exe-selfhost "${input}" "${exe}")
    endif()
    execute_process(COMMAND "${CMAKE_COMMAND}" -E env "VPP_HOME=${VPP_ROOT}"
        ${negative_command}
        RESULT_VARIABLE result ERROR_VARIABLE errors TIMEOUT 60)
    if (result EQUAL 0 OR EXISTS "${exe}")
        message(FATAL_ERROR "native parser accepted ${name}: ${result}\n${errors}")
    endif()
    if (NOT DEFINED VPP_STAGE1 AND NOT errors MATCHES "${diagnostic}")
        message(FATAL_ERROR "native parser ${name} lost diagnostic: ${errors}")
    endif()
endfunction()

foreach(name IN ITEMS byte_đẩy_i32 byte_đọc_i32 byte_ghi_i32
                      vpp_native_bytes_push_i32 vpp_native_bytes_load_i32
                      vpp_native_bytes_store_i32)
    expect_native_rejection("reserved-${name}"
        "hàm ${name}() { trả về 0; }; hàm chính() { trả về 0; };" "dành riêng")
endforeach()
expect_native_rejection("bad-parser-arity"
    "hàm chính() { trả về byte_ghi_i32(1, 2); };" "sai số đối số")
if (DEFINED VPP_STAGE1 AND NOT "${VPP_STAGE1}" STREQUAL "")
    expect_native_rejection("reserved-máy_chia_i32"
        "hàm máy_chia_i32() { trả về 0; }; hàm chính() { trả về 0; };" "dành riêng")
    expect_native_rejection("bad-native-div-arity"
        "hàm chính() { trả về máy_chia_i32(52); };" "sai số đối số")
endif()
message(STATUS "V++ native tokenizer + parser + semantic/i32 IR: PASS")
