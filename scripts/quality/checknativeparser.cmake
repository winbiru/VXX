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
execute_process(COMMAND "${CMAKE_COMMAND}" -E env "VPP_HOME=${VPP_ROOT}"
    "${VPP_EXEC}" build --target "${VPP_TARGET}" --emit-exe-selfhost
    "${source}" "${executable}"
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

# Native intrinsics remain reserved and must enforce arity before link.
foreach(name IN ITEMS byte_đẩy_i32 byte_đọc_i32 byte_ghi_i32
                      vpp_native_bytes_push_i32 vpp_native_bytes_load_i32
                      vpp_native_bytes_store_i32)
    file(WRITE "${VPP_OUTPUT_DIR}/reserved-parser.vi"
        "hàm ${name}() { trả về 0; }; hàm chính() { trả về 0; };\n")
    execute_process(COMMAND "${CMAKE_COMMAND}" -E env "VPP_HOME=${VPP_ROOT}"
        "${VPP_EXEC}" build --target "${VPP_TARGET}" --emit-exe-selfhost
        "${VPP_OUTPUT_DIR}/reserved-parser.vi" "${VPP_OUTPUT_DIR}/reserved-parser"
        RESULT_VARIABLE status ERROR_VARIABLE errors TIMEOUT 180)
    if (status EQUAL 0 OR NOT errors MATCHES "dành riêng" OR EXISTS "${VPP_OUTPUT_DIR}/reserved-parser")
        message(FATAL_ERROR "native i32 symbol ${name} must be reserved: ${status}\n${errors}")
    endif()
endforeach()
file(WRITE "${VPP_OUTPUT_DIR}/bad-parser-arity.vi"
    "hàm chính() { trả về byte_ghi_i32(1, 2); };\n")
execute_process(COMMAND "${CMAKE_COMMAND}" -E env "VPP_HOME=${VPP_ROOT}"
    "${VPP_EXEC}" build --target "${VPP_TARGET}" --emit-exe-selfhost
    "${VPP_OUTPUT_DIR}/bad-parser-arity.vi" "${VPP_OUTPUT_DIR}/bad-parser-arity"
    RESULT_VARIABLE status ERROR_VARIABLE errors TIMEOUT 180)
if (status EQUAL 0 OR NOT errors MATCHES "sai số đối số" OR EXISTS "${VPP_OUTPUT_DIR}/bad-parser-arity")
    message(FATAL_ERROR "native i32 arity must fail: ${status}\n${errors}")
endif()
message(STATUS "V++ native tokenizer + parser + semantic/i32 IR: PASS")
