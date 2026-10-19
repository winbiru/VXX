if (NOT DEFINED VPP_EXEC OR NOT DEFINED VPP_SOURCE OR
    NOT DEFINED VPP_TARGET OR NOT DEFINED VPP_OUTPUT_DIR OR NOT DEFINED VPP_HOME)
    message(FATAL_ERROR "missing native byte/file I/O arguments")
endif()
file(MAKE_DIRECTORY "${VPP_OUTPUT_DIR}")
set(executable "${VPP_OUTPUT_DIR}/native byte file")
execute_process(
    COMMAND "${CMAKE_COMMAND}" -E env "VPP_HOME=${VPP_HOME}"
        "${VPP_EXEC}" build --target "${VPP_TARGET}" --emit-exe-selfhost
        "${VPP_SOURCE}" "${executable}"
    WORKING_DIRECTORY "${VPP_OUTPUT_DIR}"
    RESULT_VARIABLE status ERROR_VARIABLE errors TIMEOUT 180)
if (NOT status EQUAL 0 OR NOT EXISTS "${executable}")
    message(FATAL_ERROR "native byte/file compile failed: ${status}\n${errors}")
endif()

file(REMOVE "${VPP_OUTPUT_DIR}/vpp-io.in" "${VPP_OUTPUT_DIR}/vpp-io.out")
execute_process(COMMAND "${executable}"
    WORKING_DIRECTORY "${VPP_OUTPUT_DIR}"
    RESULT_VARIABLE status OUTPUT_VARIABLE output ERROR_VARIABLE errors TIMEOUT 30)
if (NOT status EQUAL 0 OR NOT output STREQUAL "4\n330\n-1\n-2\n")
    message(FATAL_ERROR "native byte/file runtime failed: ${status}\n${errors}\n${output}")
endif()
foreach(name IN ITEMS vpp-io.in vpp-io.out)
    if (NOT EXISTS "${VPP_OUTPUT_DIR}/${name}")
        message(FATAL_ERROR "native byte/file did not create ${name}")
    endif()
    file(READ "${VPP_OUTPUT_DIR}/${name}" content HEX)
    if (NOT content STREQUAL "00ff0a41")
        message(FATAL_ERROR "native byte/file ${name} wrong bytes: ${content}")
    endif()
endforeach()

# Neither a bad ABI call nor a reserved runtime name may publish an executable.
file(WRITE "${VPP_OUTPUT_DIR}/bad-arity.vi"
    "hàm chính() { trả về byte_thêm(1); };\n")
execute_process(COMMAND "${CMAKE_COMMAND}" -E env "VPP_HOME=${VPP_HOME}"
    "${VPP_EXEC}" build --target "${VPP_TARGET}" --emit-exe-selfhost
    "${VPP_OUTPUT_DIR}/bad-arity.vi" "${VPP_OUTPUT_DIR}/bad-arity"
    RESULT_VARIABLE status ERROR_VARIABLE errors TIMEOUT 180)
if (status EQUAL 0 OR NOT errors MATCHES "sai số đối số" OR EXISTS "${VPP_OUTPUT_DIR}/bad-arity")
    message(FATAL_ERROR "native must reject byte ABI arity: ${status}\n${errors}")
endif()
foreach(name IN ITEMS byte_mới tệp_đóng vpp_native_bytes_get vpp_native_file_close)
    file(WRITE "${VPP_OUTPUT_DIR}/reserved.vi"
        "hàm ${name}(a) { trả về a; }; hàm chính() { trả về 0; };\n")
    execute_process(COMMAND "${CMAKE_COMMAND}" -E env "VPP_HOME=${VPP_HOME}"
        "${VPP_EXEC}" build --target "${VPP_TARGET}" --emit-exe-selfhost
        "${VPP_OUTPUT_DIR}/reserved.vi" "${VPP_OUTPUT_DIR}/reserved"
        RESULT_VARIABLE status ERROR_VARIABLE errors TIMEOUT 180)
    if (status EQUAL 0 OR NOT errors MATCHES "dành riêng" OR EXISTS "${VPP_OUTPUT_DIR}/reserved")
        message(FATAL_ERROR "native must reserve runtime symbol ${name}: ${status}\n${errors}")
    endif()
endforeach()
message(STATUS "V++ native byte buffers and file I/O ABI: PASS")
