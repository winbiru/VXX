foreach(required IN ITEMS VPP_EXEC VPP_ROOT VPP_TARGET VPP_OUTPUT_DIR)
    if (NOT DEFINED ${required})
        message(FATAL_ERROR "VM ASM bootstrap missing ${required}")
    endif()
endforeach()
set(VPP_PREPARE_ONLY TRUE)
include("${CMAKE_CURRENT_LIST_DIR}/checknativedriver.cmake")
set(vm "${VPP_OUTPUT_DIR}/vpp-vm")
execute_process(
    COMMAND "${compiler}" build "${VPP_ROOT}/gói/máy ảo/chính.vi" -o "${vm}"
    WORKING_DIRECTORY "${VPP_ROOT}"
    RESULT_VARIABLE status OUTPUT_VARIABLE output ERROR_VARIABLE errors TIMEOUT 600)
if (NOT status STREQUAL "0" OR NOT EXISTS "${vm}")
    message(FATAL_ERROR "V++/ASM VM build failed (${status}):\n${output}\n${errors}")
endif()
message(STATUS "Standalone V++/ASM VM: ${vm}")
