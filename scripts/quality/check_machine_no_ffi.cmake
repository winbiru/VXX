# POSIX Machine ABI acceptance: run the same public .vi fixtures without
# libffi.  An installed OS TLS provider is allowed, but libffi is not.
foreach(required IN ITEMS VPP_CLI VPP_ROOT)
    if (NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "Machine/no-libffi smoke requires ${required}")
    endif()
endforeach()

if (APPLE)
    execute_process(COMMAND otool -L "${VPP_CLI}"
        RESULT_VARIABLE deps_status OUTPUT_VARIABLE dependencies
        ERROR_VARIABLE deps_error)
elseif (UNIX)
    execute_process(COMMAND ldd "${VPP_CLI}"
        RESULT_VARIABLE deps_status OUTPUT_VARIABLE dependencies
        ERROR_VARIABLE deps_error)
else()
    message(FATAL_ERROR "Machine/no-libffi dependency audit currently supports POSIX")
endif()
if (NOT deps_status STREQUAL "0")
    message(FATAL_ERROR "Failed to inspect native dependencies: ${deps_error}")
endif()
if (dependencies MATCHES "libffi")
    message(FATAL_ERROR "Machine-only binary still links libffi: ${dependencies}")
endif()

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
    execute_process(
        COMMAND "${CMAKE_COMMAND}" -E env
            "VPP_HOME=${VPP_ROOT}"
            "VPP_STRICT_SYSTEM_FFI_POSIX=1"
            "VPP_FFI_ENV_TEST=vpp-env-ffi-ok"
            "VPP_FFI_ENV_EMPTY_TEST="
            "TZ=EST5EDT,M3.2.0/2,M11.1.0/2"
            "${VPP_CLI}" "${VPP_ROOT}/src/tests/${name}.vi"
        WORKING_DIRECTORY "${VPP_ROOT}"
        RESULT_VARIABLE status OUTPUT_VARIABLE actual ERROR_VARIABLE diagnostics
        TIMEOUT 60)
    file(READ "${VPP_ROOT}/src/tests/expected/${name}.expected" expected)
    if (NOT status STREQUAL "0" OR NOT actual STREQUAL expected)
        message(FATAL_ERROR "Machine/no-libffi fixture ${name} failed (${status}):\n${diagnostics}\nActual:\n${actual}\nExpected:\n${expected}")
    endif()
    math(EXPR count "${count} + 1")
endforeach()
message(STATUS "Machine ABI without libffi: ${count}/${count} .vi fixtures passed")

# A standalone executable links through scripts/quality/native_aot_link,
# which must inherit the no-libffi setting of the selected runtime archives.
# Otherwise interpreter smoke may pass while shipped AOT programs still
# require libffi at load time.
if (NOT DEFINED VPP_AOT_OUTPUT_DIR OR "${VPP_AOT_OUTPUT_DIR}" STREQUAL "")
    set(VPP_AOT_OUTPUT_DIR "${VPP_ROOT}/build/machine-noffi-aot")
endif()
file(MAKE_DIRECTORY "${VPP_AOT_OUTPUT_DIR}")
set(aot_count 0)
foreach(name IN ITEMS kiem_tra_ffi_dns kiem_tra_ffi_process_spawn kiem_tra_ffi_tls)
    set(executable "${VPP_AOT_OUTPUT_DIR}/${name}")
    file(REMOVE "${executable}")
    execute_process(
        COMMAND "${CMAKE_COMMAND}" -E env "VPP_HOME=${VPP_ROOT}"
            "${VPP_CLI}" --emit-native "${VPP_ROOT}/src/tests/${name}.vi" "${executable}"
        WORKING_DIRECTORY "${VPP_AOT_OUTPUT_DIR}"
        RESULT_VARIABLE compile_status OUTPUT_VARIABLE compile_output
        ERROR_VARIABLE compile_errors TIMEOUT 180)
    if (NOT compile_status STREQUAL "0" OR NOT EXISTS "${executable}")
        message(FATAL_ERROR "Machine/no-libffi AOT compile ${name} failed (${compile_status}):\n${compile_output}\n${compile_errors}")
    endif()
    if (APPLE)
        execute_process(COMMAND otool -L "${executable}"
            RESULT_VARIABLE aot_deps_status OUTPUT_VARIABLE aot_deps
            ERROR_VARIABLE aot_deps_error)
    else()
        execute_process(COMMAND ldd "${executable}"
            RESULT_VARIABLE aot_deps_status OUTPUT_VARIABLE aot_deps
            ERROR_VARIABLE aot_deps_error)
    endif()
    if (NOT aot_deps_status STREQUAL "0" OR aot_deps MATCHES "libffi")
        message(FATAL_ERROR "Machine/no-libffi AOT dependency audit ${name} failed (${aot_deps_status}):\n${aot_deps}\n${aot_deps_error}")
    endif()
    execute_process(
        COMMAND "${CMAKE_COMMAND}" -E env
            "VPP_HOME=${VPP_ROOT}"
            "VPP_STRICT_SYSTEM_FFI_POSIX=1"
            "TZ=EST5EDT,M3.2.0/2,M11.1.0/2"
            "${executable}"
        WORKING_DIRECTORY "${VPP_AOT_OUTPUT_DIR}"
        RESULT_VARIABLE run_status OUTPUT_VARIABLE actual
        ERROR_VARIABLE run_errors TIMEOUT 60)
    file(READ "${VPP_ROOT}/src/tests/expected/${name}.expected" expected)
    if (NOT run_status STREQUAL "0" OR NOT actual STREQUAL expected)
        message(FATAL_ERROR "Machine/no-libffi AOT ${name} failed (${run_status}):\n${run_errors}\nExpected:\n${expected}\nActual:\n${actual}")
    endif()
    math(EXPR aot_count "${aot_count} + 1")
endforeach()
message(STATUS "Machine ABI without libffi: ${aot_count}/${aot_count} native AOT fixtures passed")
