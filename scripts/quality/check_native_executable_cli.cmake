foreach(required IN ITEMS VPP_EXEC VPP_NATIVE_COMPILER VPP_TARGET VPP_ROOT VPP_OUTPUT_DIR)
    if (NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "missing ${required} for native executable CLI")
    endif()
endforeach()
if (NOT EXISTS "${VPP_NATIVE_COMPILER}")
    message(FATAL_ERROR "V++ native compiler missing: ${VPP_NATIVE_COMPILER}")
endif()

file(MAKE_DIRECTORY "${VPP_OUTPUT_DIR}")
set(source "${VPP_OUTPUT_DIR}/native-source.vi")
set(output "${VPP_OUTPUT_DIR}/program with spaces")
file(WRITE "${source}" "hàm chính() { trả về 42; };\nin chính();\n")
file(REMOVE "${output}")

# A bad VPP_NATIVE_CC validates that the CLI delegates to the bootstrapped
# V++ compiler rather than Stage 0's configurable bootstrap cc. The native
# compiler's own AOT host still links C/C++ runtime components.
execute_process(
    COMMAND "${CMAKE_COMMAND}" -E env
        "VPP_HOME=${VPP_ROOT}"
        "VPP_NATIVE_COMPILER=${VPP_NATIVE_COMPILER}"
        "VPP_NATIVE_CC=${VPP_OUTPUT_DIR}/missing C linker"
        "${VPP_EXEC}" build --target "${VPP_TARGET}" --emit-exe-vpp
            "${source}" "${output}"
    RESULT_VARIABLE status OUTPUT_VARIABLE stdout ERROR_VARIABLE stderr)
if (NOT status EQUAL 0 OR NOT EXISTS "${output}")
    message(FATAL_ERROR "native V++ executable CLI build failed: ${status} ${stdout} ${stderr}")
endif()
execute_process(COMMAND "${output}" RESULT_VARIABLE status
    OUTPUT_VARIABLE stdout ERROR_VARIABLE stderr)
if (NOT status EQUAL 0 OR NOT stdout STREQUAL "42\n")
    message(FATAL_ERROR "V++/ASM executable must print 42: ${status} ${stdout} ${stderr}")
endif()

# Verify the CLI rejects non-host targets and source aliases without
# altering an executable or the source that already exists.
file(SHA256 "${output}" before)
if (VPP_TARGET MATCHES "linux")
    set(other_target "aarch64-apple-darwin")
else()
    set(other_target "x86_64-linux-gnu")
endif()
execute_process(
    COMMAND "${CMAKE_COMMAND}" -E env "VPP_HOME=${VPP_ROOT}"
        "VPP_NATIVE_COMPILER=${VPP_NATIVE_COMPILER}"
        "${VPP_EXEC}" build --target "${other_target}" --emit-exe-vpp
            "${source}" "${output}"
    RESULT_VARIABLE status OUTPUT_VARIABLE stdout ERROR_VARIABLE stderr)
file(SHA256 "${output}" after)
if (status EQUAL 0 OR NOT before STREQUAL after OR
    NOT stderr MATCHES "chỉ liên kết executable cho host")
    message(FATAL_ERROR "cross-host request should reject and preserve output: ${status} ${stdout} ${stderr}")
endif()

set(alias "${VPP_OUTPUT_DIR}/source symlink")
file(REMOVE "${alias}")
file(CREATE_LINK "${source}" "${alias}" SYMBOLIC)
file(SHA256 "${source}" source_before)
execute_process(
    COMMAND "${CMAKE_COMMAND}" -E env "VPP_HOME=${VPP_ROOT}"
        "VPP_NATIVE_COMPILER=${VPP_NATIVE_COMPILER}"
        "${VPP_EXEC}" build --target "${VPP_TARGET}" --emit-exe-vpp
            "${source}" "${alias}"
    RESULT_VARIABLE status OUTPUT_VARIABLE stdout ERROR_VARIABLE stderr)
file(SHA256 "${source}" source_after)
if (status EQUAL 0 OR NOT source_before STREQUAL source_after OR
    NOT stderr MATCHES "ghi đè mã nguồn")
    message(FATAL_ERROR "source alias should reject without overwrite: ${status} ${stdout} ${stderr}")
endif()
