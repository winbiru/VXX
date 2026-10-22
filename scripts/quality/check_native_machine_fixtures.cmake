# Run public V++ system fixtures as standalone native executables. The caller
# supplies a freshly bootstrapped V++ native compiler; an old installed
# vpp-native binary may not know the current Machine ABI.
foreach(required IN ITEMS VPP_NATIVE_CLI VPP_ROOT VPP_OUTPUT_DIR)
    if (NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "Native Machine fixture runner requires ${required}")
    endif()
endforeach()
if (NOT EXISTS "${VPP_NATIVE_CLI}")
    message(FATAL_ERROR "Native Machine compiler missing: ${VPP_NATIVE_CLI}")
endif()
if (NOT DEFINED VPP_HOME OR "${VPP_HOME}" STREQUAL "")
    set(VPP_HOME "${VPP_ROOT}")
endif()

file(MAKE_DIRECTORY "${VPP_OUTPUT_DIR}")
set(fixtures
    kiem_tra_ffi_moi_truong
    kiem_tra_ffi_don_dieu
    kiem_tra_ffi_utc
    kiem_tra_ffi_gio_dia_phuong
    kiem_tra_ffi_ngu
    kiem_tra_ffi_entropy
    kiem_tra_ffi_process_spawn
    kiem_tra_ffi_dns
    kiem_tra_ffi_tep
    kiem_tra_ffi_thu_muc
    kiem_tra_ffi_socket_loi
    kiem_tra_ffi_tls)

set(count 0)
foreach(name IN LISTS fixtures)
    set(source "${VPP_ROOT}/src/tests/${name}.vi")
    set(executable "${VPP_OUTPUT_DIR}/${name}")
    set(expected_file "${VPP_ROOT}/src/tests/expected/${name}.expected")
    if (NOT EXISTS "${source}" OR NOT EXISTS "${expected_file}")
        message(FATAL_ERROR "Native Machine fixture missing: ${name}")
    endif()
    file(REMOVE "${executable}")
    execute_process(
        COMMAND "${CMAKE_COMMAND}" -E env "VPP_HOME=${VPP_HOME}"
            "${VPP_NATIVE_CLI}" build "${source}" -o "${executable}"
        WORKING_DIRECTORY "${VPP_OUTPUT_DIR}"
        RESULT_VARIABLE compile_status OUTPUT_VARIABLE compiled
        ERROR_VARIABLE compile_errors TIMEOUT 180)
    if (NOT compile_status STREQUAL "0" OR NOT EXISTS "${executable}")
        message(FATAL_ERROR "Native Machine compile ${name} failed (${compile_status}):\n${compiled}\n${compile_errors}")
    endif()
    execute_process(
        COMMAND "${CMAKE_COMMAND}" -E env
            "VPP_HOME=${VPP_HOME}"
            "VPP_STRICT_SYSTEM_FFI_POSIX=1"
            "VPP_FFI_ENV_TEST=vpp-env-ffi-ok"
            "VPP_FFI_ENV_EMPTY_TEST="
            "TZ=EST5EDT,M3.2.0/2,M11.1.0/2"
            "${executable}"
        WORKING_DIRECTORY "${VPP_OUTPUT_DIR}"
        RESULT_VARIABLE run_status OUTPUT_VARIABLE actual
        ERROR_VARIABLE runtime_errors TIMEOUT 60)
    file(READ "${expected_file}" expected)
    if (NOT run_status STREQUAL "0" OR NOT actual STREQUAL expected)
        message(FATAL_ERROR "Native Machine fixture ${name} failed (${run_status}):\n${runtime_errors}\nExpected:\n${expected}\nActual:\n${actual}")
    endif()
    math(EXPR count "${count} + 1")
    message(STATUS "Native Machine: ${name} PASS")
endforeach()
message(STATUS "Native Machine system fixtures: ${count}/${count} PASS")
