if (NOT DEFINED VPP_EXEC OR NOT DEFINED VPP_SOURCE OR
    NOT DEFINED VPP_CXX OR NOT DEFINED VPP_TARGET OR
    NOT DEFINED VPP_OUTPUT_DIR OR NOT DEFINED VPP_SMOKE_SOURCE)
    message(FATAL_ERROR "missing arguments for host native-object link test")
endif()

file(MAKE_DIRECTORY "${VPP_OUTPUT_DIR}")
set(executable "${VPP_OUTPUT_DIR}/vpp-native-link-smoke")

set(objects)
foreach(func IN ITEMS tra42 tinh_hang tinh_am tinh_sau cong cong_dao tru)
    set(object_path "${VPP_OUTPUT_DIR}/vpp-${func}.o")
    execute_process(
        COMMAND "${VPP_EXEC}" build --target "${VPP_TARGET}" --emit-obj
            "${func}" "${VPP_SOURCE}" "${object_path}"
        RESULT_VARIABLE result OUTPUT_VARIABLE stdout ERROR_VARIABLE stderr)
    if (NOT result EQUAL 0)
        message(FATAL_ERROR "V++ object generation failed (${func}): ${stderr}")
    endif()
    list(APPEND objects "${object_path}")
endforeach()

execute_process(
    COMMAND "${VPP_CXX}" "${VPP_SMOKE_SOURCE}" ${objects} -o "${executable}"
    RESULT_VARIABLE result OUTPUT_VARIABLE stdout ERROR_VARIABLE stderr)
if (NOT result EQUAL 0)
    message(FATAL_ERROR "host linker refused V++ generated objects: ${stderr}")
endif()

execute_process(
    COMMAND "${executable}"
    RESULT_VARIABLE result OUTPUT_VARIABLE stdout ERROR_VARIABLE stderr)
if (NOT result EQUAL 0)
    message(FATAL_ERROR "linked native machine code produced wrong result: ${result} ${stderr}")
endif()
