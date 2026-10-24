if (NOT DEFINED VPP_EXEC OR NOT DEFINED VPP_SOURCE OR NOT DEFINED VPP_TARGET OR
    NOT DEFINED VPP_OUTPUT_DIR OR NOT DEFINED VPP_HOME)
    message(FATAL_ERROR "missing native byte-range test arguments")
endif()
file(MAKE_DIRECTORY "${VPP_OUTPUT_DIR}")
include("${CMAKE_CURRENT_LIST_DIR}/native_stage1_compile.cmake")
set(executable "${VPP_OUTPUT_DIR}/native-byte-ranges")
file(REMOVE "${executable}" "${VPP_OUTPUT_DIR}/native-slice-cap.bin")
vpp_native_compile_command("${VPP_SOURCE}" "${executable}" compile_command)
execute_process(COMMAND "${CMAKE_COMMAND}" -E env "VPP_HOME=${VPP_HOME}"
    ${compile_command}
    WORKING_DIRECTORY "${VPP_OUTPUT_DIR}"
    RESULT_VARIABLE status ERROR_VARIABLE errors TIMEOUT 180)
if (NOT status EQUAL 0 OR NOT EXISTS "${executable}")
    message(FATAL_ERROR "native byte-range compilation failed: ${status}\n${errors}")
endif()
execute_process(COMMAND "${executable}"
    WORKING_DIRECTORY "${VPP_OUTPUT_DIR}"
    RESULT_VARIABLE status OUTPUT_VARIABLE output ERROR_VARIABLE errors TIMEOUT 30)
if (NOT status EQUAL 0 OR NOT output STREQUAL "1048576\n90\n")
    message(FATAL_ERROR "native byte-range execution failed: ${status}\n${errors}\n${output}")
endif()
file(READ "${VPP_OUTPUT_DIR}/native-slice-cap.bin" content HEX)
if (NOT content STREQUAL "41")
    message(FATAL_ERROR "native byte-range fixture wrote unexpected bytes: ${content}")
endif()
file(WRITE "${VPP_OUTPUT_DIR}/bad-slice-arity.vi"
    "hàm chính() { trả về byte cắt(1, 0); };\n")
file(REMOVE "${VPP_OUTPUT_DIR}/bad-slice-arity")
vpp_native_compile_command("${VPP_OUTPUT_DIR}/bad-slice-arity.vi"
    "${VPP_OUTPUT_DIR}/bad-slice-arity" compile_command)
execute_process(COMMAND "${CMAKE_COMMAND}" -E env "VPP_HOME=${VPP_HOME}"
    ${compile_command}
    RESULT_VARIABLE status ERROR_VARIABLE errors TIMEOUT 180)
if (status EQUAL 0 OR EXISTS "${VPP_OUTPUT_DIR}/bad-slice-arity" OR
    (NOT DEFINED VPP_STAGE1 AND NOT errors MATCHES "sai số đối số"))
    message(FATAL_ERROR "native slice arity must fail: ${status}\n${errors}")
endif()
foreach(name IN ITEMS "byte cắt" "byte tìm" "byte làm rỗng"
                      vpp_native_bytes_slice vpp_native_bytes_find vpp_native_bytes_clear)
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
        message(FATAL_ERROR "native byte-range symbol ${name} must be reserved: ${status}\n${errors}")
    endif()
endforeach()
message(STATUS "V++ native byte slices, search, reset and 1 MiB boundary: PASS")
