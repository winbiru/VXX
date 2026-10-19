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

file(WRITE "${VPP_OUTPUT_DIR}/unsupported.vi"
    "hàm bad(a) { trả về a / 2; };\n")
set(unwanted "${VPP_OUTPUT_DIR}/unsupported.o")
file(REMOVE "${unwanted}")
execute_process(
    COMMAND "${CMAKE_COMMAND}" -E env "VPP_HOME=${VPP_HOME}"
        "${VPP_EXEC}" build --target "${VPP_TARGET}" --emit-obj-selfhost
        bad "${VPP_OUTPUT_DIR}/unsupported.vi" "${unwanted}"
    WORKING_DIRECTORY "${VPP_OUTPUT_DIR}"
    RESULT_VARIABLE status ERROR_VARIABLE errors)
if (status EQUAL 0 OR EXISTS "${unwanted}" OR NOT errors MATCHES "chưa hỗ trợ")
    message(FATAL_ERROR "V++ native self-host must reject unsupported division: ${status} ${errors}")
endif()
