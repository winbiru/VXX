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
if(DEFINED VPP_NATIVE_COMPILER AND NOT "${VPP_NATIVE_COMPILER}" STREQUAL "")
    set(native_cli "${VPP_NATIVE_COMPILER}")
elseif(NOT "$ENV{VPP_NATIVE_COMPILER}" STREQUAL "")
    set(native_cli "$ENV{VPP_NATIVE_COMPILER}")
else()
    set(native_cli "${VPP_OUTPUT_DIR}/provider-compiler/vpp-native")
    execute_process(COMMAND "${CMAKE_COMMAND}" "-DVPP_CLI=${VPP_EXEC}"
        "-DVPP_ROOT=${VPP_ROOT}" "-DVPP_OUTPUT_DIR=${VPP_OUTPUT_DIR}/provider-compiler"
        "-DVPP_EXECUTABLE_SUFFIX=" -P "${CMAKE_CURRENT_LIST_DIR}/prepare_native_compiler.cmake"
        RESULT_VARIABLE status OUTPUT_VARIABLE output ERROR_VARIABLE errors TIMEOUT 420)
    if(NOT status STREQUAL "0")
        message(FATAL_ERROR "V++ DNS provider compiler bootstrap: ${status}\n${output}\n${errors}")
    endif()
endif()
execute_process(COMMAND "${native_cli}" vm-build "${VPP_ROOT}/gói/máy ảo/nhà cung cấp DNS.vi"
    -o "${VPP_OUTPUT_DIR}/dns-provider.vmb"
    RESULT_VARIABLE status OUTPUT_VARIABLE output ERROR_VARIABLE errors TIMEOUT 180)
if(NOT status STREQUAL "0")
    message(FATAL_ERROR "V++ DNS provider build: ${status}\n${output}\n${errors}")
endif()
message(STATUS "Standalone V++/ASM VM: ${vm}")
