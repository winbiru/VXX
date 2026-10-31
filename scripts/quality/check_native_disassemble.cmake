# Byte-for-byte disassembly parity between Stage 0, Stage 1 and CLI default.
foreach(required IN ITEMS VPP_EXEC VPP_NATIVE_COMPILER VPP_ROOT VPP_OUTPUT_DIR)
    if (NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "Disassembler parity: missing ${required}")
    endif()
endforeach()
if (NOT EXISTS "${VPP_NATIVE_COMPILER}")
    message(FATAL_ERROR "Disassembler parity: native compiler missing")
endif()
file(MAKE_DIRECTORY "${VPP_OUTPUT_DIR}")

set(source "${VPP_OUTPUT_DIR}/disassemble-fixture.vi")
file(WRITE "${source}" "hàm cộng(x, y = 2) { trả về x + y; };\n"
    "hàm chính() { in \"tiếng Việt\"; in cộng(40); };\n"
    "in \"chuỗi\\nUTF-8\";\nin 3.25;\n")
# Cross the usual unordered_map rehash boundary and include two-digit IDs.
foreach(id RANGE 2 15)
    file(APPEND "${source}" "hàm phụ${id}() { trả về ${id}; };\n")
endforeach()
file(SHA256 "${source}" original_hash)

function(run_disassembler label)
    execute_process(
        COMMAND "${CMAKE_COMMAND}" -E env "VPP_HOME=${VPP_ROOT}"
            "VPP_NATIVE_COMPILER=${VPP_NATIVE_COMPILER}" ${ARGN}
        WORKING_DIRECTORY "${VPP_OUTPUT_DIR}"
        RESULT_VARIABLE status OUTPUT_VARIABLE output ERROR_VARIABLE errors
        TIMEOUT 240)
    if (NOT status STREQUAL "0" OR output STREQUAL "")
        message(FATAL_ERROR "Disassembler ${label} failed (${status}): ${errors}")
    endif()
    file(WRITE "${VPP_OUTPUT_DIR}/${label}.txt" "${output}")
endfunction()

run_disassembler(stage0 "${VPP_EXEC}" --giải-mã --compiler=cpp "${source}")
run_disassembler(stage1 "${VPP_EXEC}" --giải-mã --compiler=vpp "${source}")
run_disassembler(default "${VPP_EXEC}" --giải-mã "${source}")
run_disassembler(native "${VPP_NATIVE_COMPILER}" disassemble "${source}")
file(READ "${VPP_OUTPUT_DIR}/stage0.txt" oracle)
foreach(label IN ITEMS stage1 default native)
    file(READ "${VPP_OUTPUT_DIR}/${label}.txt" actual)
    if (NOT actual STREQUAL oracle)
        message(FATAL_ERROR "Disassembler ${label} differs from Stage 0")
    endif()
endforeach()
if (NOT oracle MATCHES "OP_CHUOI" OR NOT oracle MATCHES "bể chuỗi=")
    message(FATAL_ERROR "Disassembler test did not cover string pool")
endif()
if (NOT oracle MATCHES "OP_PARAM_MAC_DINH" OR NOT oracle MATCHES "tiếng Việt")
    message(FATAL_ERROR "Disassembler test did not cover function bodies")
endif()
string(REGEX MATCHALL "\nfunction [0-9]+:\n" function_headers "${oracle}")
list(LENGTH function_headers function_count)
if (NOT function_count EQUAL 16)
    message(FATAL_ERROR "Disassembler omitted function bodies: ${function_count}/16")
endif()
foreach(id RANGE 0 15)
    list(GET function_headers ${id} header)
    if (NOT header STREQUAL "\nfunction ${id}:\n")
        message(FATAL_ERROR "Disassembler function order is not numeric: ${header}")
    endif()
endforeach()
file(SHA256 "${source}" final_hash)
if (NOT final_hash STREQUAL original_hash)
    message(FATAL_ERROR "Disassembler modified its source")
endif()

file(WRITE "${VPP_OUTPUT_DIR}/invalid.vi" "hàm x( {\n")
foreach(choice IN ITEMS cpp vpp)
    execute_process(
        COMMAND "${CMAKE_COMMAND}" -E env "VPP_HOME=${VPP_ROOT}"
            "VPP_NATIVE_COMPILER=${VPP_NATIVE_COMPILER}"
            "${VPP_EXEC}" --giải-mã "--compiler=${choice}"
            "${VPP_OUTPUT_DIR}/invalid.vi"
        RESULT_VARIABLE status OUTPUT_VARIABLE output ERROR_VARIABLE errors
        TIMEOUT 240)
    if (status STREQUAL "0")
        message(FATAL_ERROR "Disassembler ${choice} accepted malformed input")
    endif()
endforeach()
message(STATUS "Disassembler Stage 0/Stage 1/default/native parity: PASS")
