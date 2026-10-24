if (NOT DEFINED VPP_EXEC OR NOT DEFINED VPP_SOURCE OR NOT DEFINED VPP_TARGET OR
    NOT DEFINED VPP_OUTPUT_DIR OR NOT DEFINED VPP_HOME)
    message(FATAL_ERROR "missing native block I/O test arguments")
endif()
file(MAKE_DIRECTORY "${VPP_OUTPUT_DIR}")
include("${CMAKE_CURRENT_LIST_DIR}/native_stage1_compile.cmake")
set(binary "${VPP_OUTPUT_DIR}/native-bulk-exe")
set(payload "${VPP_OUTPUT_DIR}/native-block.bin")
file(REMOVE "${binary}" "${payload}")
vpp_native_compile_command("${VPP_SOURCE}" "${binary}" compile_command)
execute_process(COMMAND "${CMAKE_COMMAND}" -E env "VPP_HOME=${VPP_HOME}"
    ${compile_command}
    WORKING_DIRECTORY "${VPP_OUTPUT_DIR}"
    RESULT_VARIABLE status ERROR_VARIABLE errors TIMEOUT 180)
if (NOT status EQUAL 0 OR NOT EXISTS "${binary}")
    message(FATAL_ERROR "native bulk compile failed: ${status}\n${errors}")
endif()

execute_process(COMMAND "${binary}"
    WORKING_DIRECTORY "${VPP_OUTPUT_DIR}"
    RESULT_VARIABLE status OUTPUT_VARIABLE output ERROR_VARIABLE errors TIMEOUT 30)
if (NOT status EQUAL 0 OR NOT output STREQUAL "4096\n4096\n1820\n0\nNative UTF-8: Việt Nam\n")
    message(FATAL_ERROR "native bulk runtime mismatch: ${status}\n${errors}\n${output}")
endif()
if (NOT EXISTS "${payload}")
    message(FATAL_ERROR "native block output file missing")
endif()
file(SIZE "${payload}" payload_size)
if (NOT payload_size EQUAL 10012)
    message(FATAL_ERROR "native block output size: ${payload_size}")
endif()
# SHA256 constant independently derived from 2 x (UTF-8 'V++:\n' +
# bytes 0..255 cycling for 5001 steps), so equal input/output in the V++
# executable is not sufficient to mask a faulty bulk implementation.
file(SHA256 "${payload}" payload_sha)
if (NOT payload_sha STREQUAL "4a3fb371a00b860295a5096738c28d7dbedd444d52277cc992187b950a0e1047")
    message(FATAL_ERROR "native block output hash mismatch: ${payload_sha}")
endif()

# Rejection checks use the same frontend whitelist and Machine IR ABI gate.
file(WRITE "${VPP_OUTPUT_DIR}/bad-block-arity.vi"
    "hàm chính() { trả về tệp đọc khối(1); };\n")
file(REMOVE "${VPP_OUTPUT_DIR}/bad-block-arity")
vpp_native_compile_command("${VPP_OUTPUT_DIR}/bad-block-arity.vi"
    "${VPP_OUTPUT_DIR}/bad-block-arity" compile_command)
execute_process(COMMAND "${CMAKE_COMMAND}" -E env "VPP_HOME=${VPP_HOME}"
    ${compile_command}
    RESULT_VARIABLE status ERROR_VARIABLE errors TIMEOUT 180)
if (status EQUAL 0 OR EXISTS "${VPP_OUTPUT_DIR}/bad-block-arity" OR
    (NOT DEFINED VPP_STAGE1 AND NOT errors MATCHES "sai số đối số"))
    message(FATAL_ERROR "native bulk arity must fail: ${status}\n${errors}")
endif()
foreach(name IN ITEMS "byte nối" "byte bằng" "byte in" "tệp đọc khối" "tệp ghi khối"
                      vpp_native_bytes_concat vpp_native_file_read_block)
    file(WRITE "${VPP_OUTPUT_DIR}/reserved.vi"
        "hàm ${name}() { trả về 0; }; hàm chính() { trả về 0; };\n")
    file(REMOVE "${VPP_OUTPUT_DIR}/reserved")
    vpp_native_compile_command("${VPP_OUTPUT_DIR}/reserved.vi"
        "${VPP_OUTPUT_DIR}/reserved" compile_command)
    execute_process(COMMAND "${CMAKE_COMMAND}" -E env "VPP_HOME=${VPP_HOME}"
        ${compile_command}
        RESULT_VARIABLE status ERROR_VARIABLE errors TIMEOUT 180)
    if (status EQUAL 0 OR EXISTS "${VPP_OUTPUT_DIR}/reserved" OR
        (NOT DEFINED VPP_STAGE1 AND NOT errors MATCHES "dành riêng"))
        message(FATAL_ERROR "native bulk must reserve ${name}: ${status}\n${errors}")
    endif()
endforeach()
message(STATUS "V++ native buffered I/O, binary bytes and UTF-8: PASS")
