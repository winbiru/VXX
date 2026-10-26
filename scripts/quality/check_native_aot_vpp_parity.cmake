foreach(required IN ITEMS VPP_NATIVE_CLI VPP_CPP_ORACLE VPP_CC VPP_ROOT VPP_OUTPUT_DIR)
    if (NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "Native AOT parity missing ${required}")
    endif()
endforeach()
file(MAKE_DIRECTORY "${VPP_OUTPUT_DIR}")
execute_process(COMMAND "${VPP_CPP_ORACLE}" "${VPP_OUTPUT_DIR}"
    RESULT_VARIABLE status OUTPUT_VARIABLE output ERROR_VARIABLE errors)
if (NOT status STREQUAL "0")
    message(FATAL_ERROR "C++ AOT oracle failed: ${output} ${errors}")
endif()
execute_process(COMMAND "${CMAKE_COMMAND}" -E env "VPP_HOME=${VPP_ROOT}" "VPP_NATIVE_PROFILE=1"
    "${VPP_NATIVE_CLI}" build "${VPP_OUTPUT_DIR}/emitter.vi" -o "${VPP_OUTPUT_DIR}/emitter"
    RESULT_VARIABLE status OUTPUT_VARIABLE output ERROR_VARIABLE errors TIMEOUT 180)
if (NOT status STREQUAL "0")
    message(FATAL_ERROR "V++ AOT emitter build failed: ${output} ${errors}")
endif()
if (errors MATCHES "VPP_NATIVE_PROFILE emit-native-assembly")
    message(FATAL_ERROR "Native compiler fell back to the C++ AOT emitter")
endif()
execute_process(COMMAND "${VPP_OUTPUT_DIR}/emitter"
    RESULT_VARIABLE status OUTPUT_VARIABLE output ERROR_VARIABLE errors TIMEOUT 90)
if (NOT status STREQUAL "0")
    message(FATAL_ERROR "V++ AOT emitter failed: ${output} ${errors}")
endif()
foreach(backend IN ITEMS cpp vpp)
    execute_process(COMMAND "${VPP_CC}" -c "${VPP_OUTPUT_DIR}/${backend}.S"
        "-I${VPP_ROOT}/scripts/ASM" -o "${VPP_OUTPUT_DIR}/${backend}.o"
        RESULT_VARIABLE status OUTPUT_VARIABLE output ERROR_VARIABLE errors TIMEOUT 30)
    if (NOT status STREQUAL "0")
        message(FATAL_ERROR "${backend} assembly failed: ${output} ${errors}")
    endif()
endforeach()
execute_process(COMMAND "${CMAKE_COMMAND}" -E compare_files
    "${VPP_OUTPUT_DIR}/cpp.o" "${VPP_OUTPUT_DIR}/vpp.o" RESULT_VARIABLE status)
if (NOT status STREQUAL "0")
    message(FATAL_ERROR "C++ and V++ AOT objects differ (helpers/CFG/strings/functions/FFI)")
endif()
message(STATUS "Independent C++/V++ full-language AOT object parity: PASS")
