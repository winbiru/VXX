foreach(required IN ITEMS VPP_CLI VPP_ROOT VPP_OUTPUT_DIR)
    if(NOT DEFINED ${required})
        message(FATAL_ERROR "Backend check missing ${required}")
    endif()
endforeach()
file(MAKE_DIRECTORY "${VPP_OUTPUT_DIR}")
set(source "${VPP_OUTPUT_DIR}/valid.vi")
set(bad "${VPP_OUTPUT_DIR}/invalid.vi")
file(WRITE "${source}" "in \"VM UTF-8 🌿\";\n")
file(WRITE "${bad}" "hàm chính() { a = [1]; in a[3]; };\n")

function(run_checked)
    execute_process(COMMAND "${CMAKE_COMMAND}" -E env
        "VPP_HOME=${VPP_ROOT}"
        "VPP_NATIVE_COMPILER=${VPP_OUTPUT_DIR}/missing-native-compiler"
        "VPP_PREFERENCES_HOME=${VPP_OUTPUT_DIR}/preferences"
        "${VPP_CLI}" ${ARGN}
        RESULT_VARIABLE status OUTPUT_VARIABLE output ERROR_VARIABLE errors TIMEOUT 30)
    if(NOT status STREQUAL "0")
        message(FATAL_ERROR "Backend command failed: ${status}\n${output}\n${errors}")
    endif()
    set(result "${output}" PARENT_SCOPE)
endfunction()

run_checked(run --compiler=cpp "${source}")
if(NOT result STREQUAL "VM UTF-8 🌿\n")
    message(FATAL_ERROR "Explicit interpreter lost UTF-8: ${result}")
endif()
set(output "${VPP_OUTPUT_DIR}/cpp")
run_checked(--batch-run-tests --compiler=cpp "${output}" "${source}" "${bad}")
file(READ "${output}/valid.status" status)
string(STRIP "${status}" status)
file(READ "${output}/valid.output" actual)
string(REPLACE "\r\n" "\n" actual "${actual}")
if(NOT status STREQUAL "0" OR NOT actual STREQUAL "VM UTF-8 🌿\n")
    message(FATAL_ERROR "Batch interpreter lost UTF-8: ${status}\n${actual}")
endif()
file(READ "${output}/invalid.status" status)
string(STRIP "${status}" status)
file(READ "${output}/invalid.output" actual)
string(REPLACE "\r\n" "\n" actual "${actual}")
if(status STREQUAL "0" OR NOT actual MATCHES "^Lỗi: chỉ số vượt phạm vi\n" OR
    NOT actual MATCHES "Dấu vết lỗi:")
    message(FATAL_ERROR "Batch lost VM diagnostics: ${status}\n${actual}")
endif()

# The ordinary command must still validate the configured native compiler.
set(output "${VPP_OUTPUT_DIR}/auto")
run_checked(--batch-run-tests "${output}" "${source}")
file(READ "${output}/valid.status" status)
string(STRIP "${status}" status)
file(READ "${output}/valid.output" actual)
if(status STREQUAL "0" OR NOT actual MATCHES "VPP_NATIVE_COMPILER")
    message(FATAL_ERROR "Default backend ignored the native configuration")
endif()

# An explicitly requested V++ backend must fail closed when the configured
# native compiler is unavailable; executing successfully through C++ would
# silently invalidate every self-hosted golden test.
foreach(native_command IN ITEMS run batch)
    if(native_command STREQUAL "run")
        set(native_args run --compiler=vpp "${source}")
    else()
        set(native_args --batch-run-tests --compiler=vpp
            "${VPP_OUTPUT_DIR}/explicit-native" "${source}")
    endif()
    execute_process(COMMAND "${CMAKE_COMMAND}" -E env
        "VPP_HOME=${VPP_ROOT}"
        "VPP_NATIVE_COMPILER=${VPP_OUTPUT_DIR}/missing-native-compiler"
        "${VPP_CLI}" ${native_args}
        RESULT_VARIABLE native_status OUTPUT_VARIABLE native_output
        ERROR_VARIABLE native_errors TIMEOUT 30)
    if(native_status STREQUAL "0")
        message(FATAL_ERROR "Explicit V++ ${native_command} silently fell back to the C++ VM")
    endif()
    if(NOT "${native_output}${native_errors}" MATCHES "VPP_NATIVE_COMPILER")
        message(FATAL_ERROR "Explicit V++ ${native_command} did not report the missing native compiler: ${native_status}\n${native_output}\n${native_errors}")
    endif()
endforeach()
message(STATUS "VM backend: explicit selection, UTF-8, diagnostics; explicit V++ never falls back: PASS")
