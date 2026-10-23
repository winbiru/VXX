if (NOT DEFINED VPP_EXEC OR NOT DEFINED VPP_SOURCE OR
    NOT DEFINED VPP_CC OR NOT DEFINED VPP_TARGET OR
    NOT DEFINED VPP_OUTPUT_DIR OR NOT DEFINED VPP_SMOKE_SOURCE)
    message(FATAL_ERROR "missing arguments for native calls link smoke")
endif()

file(MAKE_DIRECTORY "${VPP_OUTPUT_DIR}")
set(objects)
foreach(func IN ITEMS co_so thêm ten_ham_dai nhan_sau ket_hop tinh_vong bon goi_bon tong_muoi goi_muoi)
    set(obj "${VPP_OUTPUT_DIR}/native-call-${func}.o")
    execute_process(
        COMMAND "${VPP_EXEC}" build --target "${VPP_TARGET}" --emit-obj
            "${func}" "${VPP_SOURCE}" "${obj}"
        RESULT_VARIABLE code ERROR_VARIABLE stderr)
    if (NOT code EQUAL 0)
        message(FATAL_ERROR "native call object failed (${func}): ${stderr}")
    endif()
    list(APPEND objects "${obj}")
endforeach()

set(executable "${VPP_OUTPUT_DIR}/native-calls-smoke")
execute_process(COMMAND "${VPP_CC}" "${VPP_SMOKE_SOURCE}" ${objects} -o "${executable}"
    RESULT_VARIABLE code ERROR_VARIABLE stderr)
if (NOT code EQUAL 0)
    message(FATAL_ERROR "native call linker failed: ${stderr}")
endif()
execute_process(COMMAND "${executable}" RESULT_VARIABLE code
    OUTPUT_VARIABLE stdout ERROR_VARIABLE stderr)
if (NOT code EQUAL 0 OR NOT stdout MATCHES "native_calls_ok")
    message(FATAL_ERROR "native call execution failed: ${code} ${stderr}")
endif()
