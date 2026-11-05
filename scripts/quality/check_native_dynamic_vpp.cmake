foreach(required IN ITEMS VPP_STAGE1 VPP_NATIVE_CLI VPP_ROOT VPP_OUTPUT_DIR)
    if (NOT DEFINED ${required})
        message(FATAL_ERROR "V++ dynamic AOT missing ${required}")
    endif()
endforeach()
file(MAKE_DIRECTORY "${VPP_OUTPUT_DIR}")
set(ENV{VPP_HOME} "${VPP_ROOT}")
if (NOT DEFINED VPP_AOT_PREPARED_OBJECT)
    execute_process(COMMAND /bin/bash "${VPP_ROOT}/scripts/build-vpp-aot-runtime.sh"
        "${VPP_OUTPUT_DIR}" "${VPP_STAGE1}"
        RESULT_VARIABLE status OUTPUT_VARIABLE output ERROR_VARIABLE errors TIMEOUT 600)
    if (NOT status STREQUAL "0")
        message(FATAL_ERROR "V++ runtime object build failed (${status}): ${output}\n${errors}")
    endif()
    set(VPP_AOT_PREPARED_OBJECT "${VPP_OUTPUT_DIR}/runtime.o")
endif()
function(check_native_fixture name source expected)
    set(executable "${VPP_OUTPUT_DIR}/${name}")
    execute_process(COMMAND "${CMAKE_COMMAND}" -E env
        VPP_AOT_RUNTIME=vpp "VPP_AOT_VPP_OBJECT=${VPP_AOT_PREPARED_OBJECT}"
        "${VPP_NATIVE_CLI}" build "${source}" -o "${executable}"
        RESULT_VARIABLE status OUTPUT_VARIABLE output ERROR_VARIABLE errors TIMEOUT 180)
    if (NOT status STREQUAL "0")
        message(FATAL_ERROR "${name} AOT build failed: ${output}\n${errors}")
    endif()
    execute_process(COMMAND "${CMAKE_COMMAND}" -E env "PATH=" "${executable}"
        WORKING_DIRECTORY "${VPP_OUTPUT_DIR}"
        RESULT_VARIABLE status OUTPUT_VARIABLE output ERROR_VARIABLE errors TIMEOUT 90)
    if (NOT status STREQUAL "0" OR NOT output STREQUAL expected)
        message(FATAL_ERROR "${name} runtime mismatch (${status}): ${output}\n${errors}")
    endif()
    # Independent runtime oracle on the same source, including GC stress.
    execute_process(COMMAND "${CMAKE_COMMAND}" -E env VPP_AOT_RUNTIME=cpp
        "${VPP_NATIVE_CLI}" build "${source}" -o "${executable}-cpp"
        RESULT_VARIABLE status OUTPUT_VARIABLE oracle_build ERROR_VARIABLE errors TIMEOUT 180)
    if (NOT status STREQUAL "0")
        message(FATAL_ERROR "${name} C++ oracle build failed: ${oracle_build}\n${errors}")
    endif()
    execute_process(COMMAND "${executable}-cpp" WORKING_DIRECTORY "${VPP_OUTPUT_DIR}"
        RESULT_VARIABLE status OUTPUT_VARIABLE oracle_output ERROR_VARIABLE errors TIMEOUT 90)
    if (NOT status STREQUAL "0" OR NOT oracle_output STREQUAL output)
        message(FATAL_ERROR "${name} differs from C++ runtime: ${oracle_output}\n${errors}")
    endif()
    execute_process(COMMAND /usr/bin/nm -g "${executable}"
        RESULT_VARIABLE status OUTPUT_VARIABLE symbols ERROR_VARIABLE errors)
    if (NOT status STREQUAL "0" OR
        symbols MATCHES "__cxa_|_ZSt|_ZNSt|_Z[0-9]|vm_step|vm_step_body|linkedBytecodeRun|executeVpp")
        message(FATAL_ERROR "${name} linked C++/bytecode dispatcher: ${symbols}\n${errors}")
    endif()
    if (NOT symbols MATCHES "_vpp_fn_aot_integer" OR NOT symbols MATCHES "_vpp_fn_vm_collect")
        message(FATAL_ERROR "${name} lacks V++ dynamic values/GC")
    endif()
    execute_process(COMMAND /usr/bin/otool -L "${executable}"
        RESULT_VARIABLE status OUTPUT_VARIABLE libraries ERROR_VARIABLE errors)
    if (NOT status STREQUAL "0" OR libraries MATCHES "libc\\+\\+|libstdc\\+\\+|libffi")
        message(FATAL_ERROR "${name} linked legacy C++/FFI libraries: ${libraries}\n${errors}")
    endif()
    file(WRITE "${executable}.dependencies.txt" "${libraries}")
endfunction()
check_native_fixture(vm-fixture "${VPP_ROOT}/src/tests/fixtures/vm_asm.vi"
    "-2147483648\n-2147483648\n-1\n3.75\n3.0\ni\n[1, 9, 3]\n16\n41\n42\n9\n25\nswitch\nngoại lệ VM\nASM VM OK\n")
check_native_fixture(dynamic-fixture "${VPP_ROOT}/src/tests/fixtures/native_dynamic_vpp.vi"
    "42\n43\n2147483647\n3.75\n2\n🐱\n[1, 9, 3]\n9\n41\n43\n9\n0\n1\n2\nđúng\n[ngoại lệ, 42]\n44\n10\n[1, 9, 3]\nVPP DYNAMIC AOT OK\n")
# Missing providers must fail before user output, with no C++ fallback.
function(check_unavailable name source)
    set(unsupported "${VPP_OUTPUT_DIR}/unsupported-${name}")
    execute_process(COMMAND "${CMAKE_COMMAND}" -E env
        VPP_AOT_RUNTIME=vpp "VPP_AOT_VPP_OBJECT=${VPP_AOT_PREPARED_OBJECT}"
        "${VPP_NATIVE_CLI}" build "${source}" -o "${unsupported}" ${ARGN}
        RESULT_VARIABLE status OUTPUT_VARIABLE output ERROR_VARIABLE errors TIMEOUT 180)
    if (NOT status STREQUAL "0")
        message(FATAL_ERROR "Unsupported ${name} fixture build failed: ${output}\n${errors}")
    endif()
    execute_process(COMMAND "${unsupported}" RESULT_VARIABLE status
        OUTPUT_VARIABLE output ERROR_VARIABLE errors TIMEOUT 10)
    if (NOT status STREQUAL "1" OR NOT output STREQUAL "" OR NOT errors MATCHES "error=22")
        message(FATAL_ERROR "Unsupported ${name} did not fail closed: ${status}\n${output}\n${errors}")
    endif()
endfunction()
check_unavailable(compiler "${VPP_ROOT}/src/tests/fixtures/native_dynamic_vpp.vi" --compiler)
check_unavailable(foreign "${VPP_ROOT}/src/tests/kiem_tra_native_ffi_binding.vi")
message(STATUS "Native functions/branches with V++ values, closures, OOP, exceptions and GC, without libc++/libffi: PASS")
