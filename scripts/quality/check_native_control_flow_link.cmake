if (NOT DEFINED VPP_EXEC OR NOT DEFINED VPP_SOURCE OR
    NOT DEFINED VPP_CC OR NOT DEFINED VPP_TARGET OR
    NOT DEFINED VPP_OUTPUT_DIR OR NOT DEFINED VPP_SMOKE_SOURCE)
    message(FATAL_ERROR "missing arguments for native-control-flow link smoke")
endif()

file(MAKE_DIRECTORY "${VPP_OUTPUT_DIR}")
set(objects)
foreach(func IN ITEMS tinh tong tong_khi dieu_khien_khi max_i32 tong_bon tru_dao chia_i32 chia_qua_ham phu_dinh phan_loai tim_dau dieu_kien bo_qua register_limit register_overflow hoist_vong hoist_hai_vong hoist_long_nhau hoist_goi)
    set(obj "${VPP_OUTPUT_DIR}/machine-${func}.o")
    execute_process(
        COMMAND "${VPP_EXEC}" build --target "${VPP_TARGET}" --emit-obj
            "${func}" "${VPP_SOURCE}" "${obj}"
        RESULT_VARIABLE code ERROR_VARIABLE stderr)
    if (NOT code EQUAL 0)
        message(FATAL_ERROR "native control flow object failed (${func}): ${stderr}")
    endif()
    list(APPEND objects "${obj}")
endforeach()

set(executable "${VPP_OUTPUT_DIR}/machine-control-flow-smoke")
execute_process(COMMAND "${VPP_CC}" "${VPP_SMOKE_SOURCE}" ${objects} -o "${executable}"
    RESULT_VARIABLE code ERROR_VARIABLE stderr)
if (NOT code EQUAL 0)
    message(FATAL_ERROR "native machine control flow link failed: ${stderr}")
endif()
execute_process(COMMAND "${executable}" RESULT_VARIABLE code
    OUTPUT_VARIABLE stdout ERROR_VARIABLE stderr)
if (NOT code EQUAL 0 OR NOT stdout MATCHES "native_control_flow_ok")
    message(FATAL_ERROR "native machine control flow execution failed: ${code} ${stderr}")
endif()
