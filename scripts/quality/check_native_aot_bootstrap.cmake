foreach(required IN ITEMS VPP_CLI VPP_HOME VPP_SOURCE_ROOT VPP_OUTPUT_DIR)
    if (NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "Native AOT bootstrap missing ${required}")
    endif()
endforeach()

function(run_native_stage label)
    # Full compiler generation took 273 s for Stage 1 -> Stage 2 locally.
    # Leave room for slower ARM64 CI runners without weakening the probes.
    set(stage_timeout 300)
    if (label MATCHES "^Stage [012] ->")
        set(stage_timeout 600)
    endif()
    execute_process(COMMAND ${ARGN}
        WORKING_DIRECTORY "${VPP_SOURCE_ROOT}"
        RESULT_VARIABLE status OUTPUT_VARIABLE stdout ERROR_VARIABLE stderr
        TIMEOUT ${stage_timeout})
    if (NOT status STREQUAL "0")
        message(FATAL_ERROR "${label} failed (${status}):\n${stdout}\n${stderr}")
    endif()
endfunction()

file(MAKE_DIRECTORY "${VPP_OUTPUT_DIR}")
set(source "${VPP_SOURCE_ROOT}/gói/biên dịch/native_cli.vi")
set(stage1 "${VPP_OUTPUT_DIR}/stage1")
set(stage2 "${VPP_OUTPUT_DIR}/stage2")
set(stage3 "${VPP_OUTPUT_DIR}/stage3")
set(probe "${VPP_OUTPUT_DIR}/probe")

run_native_stage("Stage 0 -> Stage 1"
    "${CMAKE_COMMAND}" -E env "VPP_HOME=${VPP_HOME}"
    "${VPP_CLI}" --bootstrap-native "${source}" "${stage1}")
if (NOT EXISTS "${stage1}")
    message(FATAL_ERROR "Stage 1 compiler was not generated")
endif()

run_native_stage("Stage 1 -> Stage 2"
    "${CMAKE_COMMAND}" -E env "VPP_HOME=${VPP_HOME}"
    "${stage1}" build "${source}" -o "${stage2}" --compiler)
if (NOT EXISTS "${stage2}")
    message(FATAL_ERROR "Stage 2 compiler was not generated")
endif()

run_native_stage("Stage 2 -> Stage 3"
    "${CMAKE_COMMAND}" -E env "VPP_HOME=${VPP_HOME}"
    "${stage2}" build "${source}" -o "${stage3}" --compiler)
if (NOT EXISTS "${stage3}")
    message(FATAL_ERROR "Stage 3 compiler was not generated")
endif()

file(SHA256 "${stage2}" stage2_hash)
file(SHA256 "${stage3}" stage3_hash)
if (NOT stage2_hash STREQUAL stage3_hash)
    message(FATAL_ERROR "Native self-hosting did not converge: ${stage2_hash} != ${stage3_hash}")
endif()

run_native_stage("Stage 3 native code generation"
    "${CMAKE_COMMAND}" -E env "VPP_HOME=${VPP_HOME}"
    "${stage3}" build "${VPP_SOURCE_ROOT}/src/tests/nativeaotprint.vi" -o "${probe}")
if (NOT EXISTS "${probe}")
    message(FATAL_ERROR "Stage 3 did not produce a native probe")
endif()
execute_process(COMMAND "${probe}"
    RESULT_VARIABLE probe_status OUTPUT_VARIABLE probe_output ERROR_VARIABLE probe_error TIMEOUT 30)
if (NOT probe_status STREQUAL "0" OR NOT probe_output STREQUAL "-2147483648\n42\n0\n")
    message(FATAL_ERROR "Stage 3 probe mismatch (${probe_status}): ${probe_output} ${probe_error}")
endif()

# Verify the executable's output, not just successful assembly and linkage.
function(check_stage3_fixture label source expected target)
    set(executable "${VPP_OUTPUT_DIR}/${target}")
    run_native_stage("Stage 3 ${label} code generation"
        "${CMAKE_COMMAND}" -E env "VPP_HOME=${VPP_HOME}"
        "${stage3}" build "${source}" -o "${executable}")
    if (NOT EXISTS "${executable}")
        message(FATAL_ERROR "Stage 3 did not produce ${label}")
    endif()
    execute_process(COMMAND "${executable}"
        WORKING_DIRECTORY "${VPP_OUTPUT_DIR}"
        RESULT_VARIABLE run_status OUTPUT_VARIABLE actual ERROR_VARIABLE errors TIMEOUT 30)
    file(READ "${expected}" expected_output)
    if (NOT run_status STREQUAL "0" OR NOT actual STREQUAL expected_output)
        message(FATAL_ERROR "Stage 3 ${label} mismatch (${run_status}):\n${actual}\n${errors}")
    endif()
endfunction()

# Imported foreign declarations, buffers, descriptor IDs, and lexical shadowing.
check_stage3_fixture("FFI sleep"
    "${VPP_SOURCE_ROOT}/src/tests/kiem_tra_ffi_ngu.vi"
    "${VPP_SOURCE_ROOT}/src/tests/expected/kiem_tra_ffi_ngu.expected" "ffi-probe")
check_stage3_fixture("FFI lexical binding"
    "${VPP_SOURCE_ROOT}/src/tests/kiem_tra_native_ffi_binding.vi"
    "${VPP_SOURCE_ROOT}/src/tests/expected/kiem_tra_native_ffi_binding.expected" "ffi-binding")

# Bare-name package imports (phân giải dns -> gói/mạng/dns.vi).
check_stage3_fixture("FFI DNS package alias"
    "${VPP_SOURCE_ROOT}/src/tests/kiem_tra_ffi_dns.vi"
    "${VPP_SOURCE_ROOT}/src/tests/expected/kiem_tra_ffi_dns.expected" "ffi-dns")
check_stage3_fixture("package facade re-export"
    "${VPP_SOURCE_ROOT}/src/tests/fixtures/native_aot_facade/entry.vi"
    "${VPP_SOURCE_ROOT}/src/tests/expected/kiem_tra_native_facade.expected" "facade")

# Compile the original fixture from outside the checkout, forcing package
# lookup through the installed VPP_HOME SDK with its unmodified facade import.
set(installed_file_fixture "${VPP_OUTPUT_DIR}/ffi-file-facade.vi")
configure_file("${VPP_SOURCE_ROOT}/src/tests/kiem_tra_ffi_tep.vi"
    "${installed_file_fixture}" COPYONLY)
check_stage3_fixture("FFI file IO"
    "${installed_file_fixture}"
    "${VPP_SOURCE_ROOT}/src/tests/expected/kiem_tra_ffi_tep.expected" "ffi-file")
check_stage3_fixture("FFI directory IO"
    "${VPP_SOURCE_ROOT}/src/tests/kiem_tra_ffi_thu_muc.vi"
    "${VPP_SOURCE_ROOT}/src/tests/expected/kiem_tra_ffi_thu_muc.expected" "ffi-directory")
check_stage3_fixture("FFI TLS validation"
    "${VPP_SOURCE_ROOT}/src/tests/kiem_tra_ffi_tls.vi"
    "${VPP_SOURCE_ROOT}/src/tests/expected/kiem_tra_ffi_tls.expected" "ffi-tls")
check_stage3_fixture("FFI socket error handling"
    "${VPP_SOURCE_ROOT}/src/tests/kiem_tra_ffi_socket_loi.vi"
    "${VPP_SOURCE_ROOT}/src/tests/expected/kiem_tra_ffi_socket_loi.expected" "ffi-socket-error")
check_stage3_fixture("FFI UTC"
    "${VPP_SOURCE_ROOT}/src/tests/kiem_tra_ffi_utc.vi"
    "${VPP_SOURCE_ROOT}/src/tests/expected/kiem_tra_ffi_utc.expected" "ffi-utc")
check_stage3_fixture("FFI entropy"
    "${VPP_SOURCE_ROOT}/src/tests/kiem_tra_ffi_entropy.vi"
    "${VPP_SOURCE_ROOT}/src/tests/expected/kiem_tra_ffi_entropy.expected" "ffi-entropy")

message(STATUS "Native AOT Stage 1 -> Stage 2 -> Stage 3, FFI and package imports: PASS (${stage3_hash})")
