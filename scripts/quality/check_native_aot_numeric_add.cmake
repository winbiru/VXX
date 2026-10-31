foreach(required IN ITEMS VPP_EXEC VPP_NATIVE_COMPILER VPP_TARGET VPP_ROOT VPP_OUTPUT_DIR)
    if (NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "missing ${required} for native AOT numeric fast path")
    endif()
endforeach()

file(MAKE_DIRECTORY "${VPP_OUTPUT_DIR}")
set(source "${VPP_OUTPUT_DIR}/numeric-add.vi")
set(output "${VPP_OUTPUT_DIR}/numeric-add")
file(WRITE "${source}"
    "hàm chính() { biến f = 0.25; biến sum = 0.0; lặp(i = 0; i < 50000; i++) { sum = i + f; }; in sum; };\n")
execute_process(
    COMMAND "${CMAKE_COMMAND}" -E env
        "VPP_HOME=${VPP_ROOT}"
        "VPP_NATIVE_COMPILER=${VPP_NATIVE_COMPILER}"
        "VPP_AOT_KEEP_ASM=1"
        "${VPP_EXEC}" build --target "${VPP_TARGET}" --emit-exe-vpp "${source}" "${output}"
    RESULT_VARIABLE status OUTPUT_VARIABLE stdout ERROR_VARIABLE stderr)
if (NOT status EQUAL 0 OR NOT EXISTS "${output}" OR NOT EXISTS "${output}.S")
    message(FATAL_ERROR "numeric fast path AOT compilation failed: ${status} ${stdout} ${stderr}")
endif()
file(READ "${output}.S" assembly)
if (NOT assembly MATCHES "vpp_native_dynamic_numeric_add_loop")
    message(FATAL_ERROR "V++ self-hosted emitter did not select numeric_add_loop")
endif()

execute_process(
    COMMAND "${CMAKE_COMMAND}" -E env "VPP_AOT_CPU_PROFILE=1" "${output}"
    RESULT_VARIABLE status OUTPUT_VARIABLE stdout ERROR_VARIABLE stderr)
if (NOT status EQUAL 0 OR NOT stdout STREQUAL "49999.25\n")
    message(FATAL_ERROR "numeric fast path produced wrong result: ${status} ${stdout} ${stderr}")
endif()
if (NOT stderr MATCHES "abi.numeric_add_loop calls=1 ")
    message(FATAL_ERROR "numeric fast path did not execute exactly once: ${stderr}")
endif()
if (stderr MATCHES "abi.update_numeric_slots calls=[1-9]" OR
    stderr MATCHES "abi.increment_slot calls=[1-9]")
    message(FATAL_ERROR "numeric fast path fell through into per-iteration ABI: ${stderr}")
endif()
