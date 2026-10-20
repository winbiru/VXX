foreach(required IN ITEMS VPP_CLI VPP_ROOT VPP_OUTPUT_DIR)
    if (NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "Native compiler setup missing ${required}")
    endif()
endforeach()
if (NOT DEFINED VPP_EXECUTABLE_SUFFIX)
    message(FATAL_ERROR "Native compiler setup missing VPP_EXECUTABLE_SUFFIX")
endif()
file(MAKE_DIRECTORY "${VPP_OUTPUT_DIR}")
# In cmake -P mode CMAKE_EXECUTABLE_SUFFIX is not initialized by a project().
# Use the suffix passed by the parent CMake configure (notably .exe on MSVC).
set(compiler "${VPP_OUTPUT_DIR}/vpp-native${VPP_EXECUTABLE_SUFFIX}")
execute_process(
    COMMAND "${CMAKE_COMMAND}" -E env "VPP_HOME=${VPP_ROOT}"
        "${VPP_CLI}" --bootstrap-native
        "${VPP_ROOT}/gói/biên dịch/native_cli.vi" "${compiler}"
    WORKING_DIRECTORY "${VPP_ROOT}"
    RESULT_VARIABLE status OUTPUT_VARIABLE stdout ERROR_VARIABLE stderr TIMEOUT 360)
if (NOT status STREQUAL "0" OR NOT EXISTS "${compiler}")
    message(FATAL_ERROR "Stage 0 -> native V++ compiler failed (${status}):\n${stdout}\n${stderr}")
endif()
message(STATUS "Native V++ compiler: ${compiler}")
