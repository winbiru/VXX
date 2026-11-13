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
    set(expected_status 0)
    if(ARGC GREATER 3)
        set(expected_status "${ARGV3}")
    endif()
    set(executable "${VPP_OUTPUT_DIR}/${name}")
    execute_process(COMMAND "${CMAKE_COMMAND}" -E env
        VPP_AOT_RUNTIME=vpp VPP_AOT_KEEP_ASM=1 "VPP_AOT_VPP_OBJECT=${VPP_AOT_PREPARED_OBJECT}"
        "${VPP_NATIVE_CLI}" build "${source}" -o "${executable}"
        RESULT_VARIABLE status OUTPUT_VARIABLE output ERROR_VARIABLE errors TIMEOUT 180)
    if (NOT status STREQUAL "0")
        message(FATAL_ERROR "${name} AOT build failed: ${output}\n${errors}")
    endif()
    execute_process(COMMAND "${CMAKE_COMMAND}" -E env "PATH=" "${executable}"
        WORKING_DIRECTORY "${VPP_OUTPUT_DIR}"
        RESULT_VARIABLE status OUTPUT_VARIABLE output ERROR_VARIABLE errors TIMEOUT 90)
    if (NOT status STREQUAL expected_status OR NOT output STREQUAL expected)
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
    if (NOT status STREQUAL expected_status OR NOT oracle_output STREQUAL output)
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
file(READ "${VPP_ROOT}/src/tests/expected/native_loop_stack_gc.expected" loop_expected)
check_native_fixture(loop-stack-gc "${VPP_ROOT}/src/tests/fixtures/native_loop_stack_gc.vi" "${loop_expected}")
file(READ "${VPP_OUTPUT_DIR}/loop-stack-gc.S" loop_asm)
foreach(id IN ITEMS 1 2 3 4)
    math(EXPR next_id "${id}+1")
    string(FIND "${loop_asm}" "_vpp_aot_function_${id}:" start)
    string(FIND "${loop_asm}" "_vpp_aot_function_${next_id}:" finish)
    if(start LESS 0 OR finish LESS_EQUAL start OR loop_asm MATCHES "vpp_aot_i32_${id}:")
        message(FATAL_ERROR "loop ${id} did not exercise the dynamic AOT path")
    endif()
    math(EXPR length "${finish}-${start}")
    string(SUBSTRING "${loop_asm}" ${start} ${length} body)
    if(id EQUAL 4)
        if(body MATCHES "vpp_native_dynamic_increment_slot_discard" OR
           NOT body MATCHES "vpp_native_dynamic_increment_slot[\n\r]" OR
           body MATCHES "vpp_native_dynamic_return_value")
            message(FATAL_ERROR "fallthrough must return null; unproven loop retains stack history")
        endif()
    elseif(NOT body MATCHES "vpp_native_dynamic_increment_slot_discard" OR
           body MATCHES "vpp_native_dynamic_increment_slot[\n\r]")
        message(FATAL_ERROR "loop ${id} did not discard its dead counter result")
    endif()
endforeach()
file(READ "${VPP_ROOT}/src/tests/expected/native_return_semantics.expected" return_expected)
check_native_fixture(return-semantics "${VPP_ROOT}/src/tests/fixtures/native_return_semantics.vi" "${return_expected}" 42)
check_native_fixture(frame-pool-gc "${VPP_ROOT}/src/tests/fixtures/native_frame_pool_gc.vi"
    "8388608\n8388608\n8388608\n8388608\n42\n43\n[lỗi, 42]\nFrame pool GC: PASS\n")
check_native_fixture(exit-scalar "${VPP_ROOT}/src/tests/fixtures/native_exit_scalar.vi" "" 42)
file(READ "${VPP_ROOT}/src/tests/expected/native_protected_loop.expected" protected_expected)
check_native_fixture(protected-loop "${VPP_ROOT}/src/tests/fixtures/native_protected_loop.vi" "${protected_expected}")
file(READ "${VPP_OUTPUT_DIR}/protected-loop.S" protected_asm)
string(FIND "${protected_asm}" "_vpp_aot_function_0:" ordinary_start)
string(FIND "${protected_asm}" "_vpp_aot_function_1:" protected_start)
string(FIND "${protected_asm}" "_vpp_aot_function_2:" protected_end)
if(ordinary_start LESS 0 OR protected_start LESS_EQUAL ordinary_start OR protected_end LESS_EQUAL protected_start)
    message(FATAL_ERROR "protected loop fixture is missing dynamic functions")
endif()
math(EXPR ordinary_length "${protected_start}-${ordinary_start}")
math(EXPR protected_length "${protected_end}-${protected_start}")
string(SUBSTRING "${protected_asm}" ${ordinary_start} ${ordinary_length} ordinary_body)
string(SUBSTRING "${protected_asm}" ${protected_start} ${protected_length} protected_body)
if(NOT ordinary_body MATCHES "vpp_native_dynamic_simple_scalar_loop_tail" OR
   NOT protected_body MATCHES "vpp_native_dynamic_simple_scalar_loop[\n\r]" OR
   protected_body MATCHES "vpp_native_dynamic_simple_scalar_loop_tail|vpp_native_dynamic_increment_slot_discard")
    message(FATAL_ERROR "scalar loop discarded counter history across an exception boundary")
endif()
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
