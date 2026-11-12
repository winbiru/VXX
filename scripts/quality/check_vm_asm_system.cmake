foreach(required IN ITEMS VPP_VM VPP_STAGE1 VPP_NATIVE_COMPILER VPP_ROOT VPP_OUTPUT_DIR)
    if (NOT DEFINED ${required})
        message(FATAL_ERROR "VM system test missing ${required}")
    endif()
endforeach()
file(MAKE_DIRECTORY "${VPP_OUTPUT_DIR}")
set(ENV{VPP_HOME} "${VPP_ROOT}")
set(source "${VPP_ROOT}/src/tests/fixtures/vm_asm_system.vi")
set(image "${VPP_OUTPUT_DIR}/system.vmb")
execute_process(COMMAND "${VPP_NATIVE_COMPILER}" vm-build "${source}" -o "${image}"
    RESULT_VARIABLE status OUTPUT_VARIABLE output ERROR_VARIABLE errors TIMEOUT 120)
if (NOT status STREQUAL "0")
    message(FATAL_ERROR "System VM fixture compilation failed: ${status}\n${output}\n${errors}")
endif()
execute_process(COMMAND "${CMAKE_COMMAND}" -E env "PATH=" "${VPP_VM}" "${image}"
    WORKING_DIRECTORY "${VPP_OUTPUT_DIR}"
    RESULT_VARIABLE status OUTPUT_VARIABLE output ERROR_VARIABLE errors TIMEOUT 60)
if (NOT status STREQUAL "0" OR NOT output STREQUAL "VM ASM SYSTEM OK\n")
    message(FATAL_ERROR "V++/ASM clock/sleep/entropy failed: ${status}\n${output}\n${errors}")
endif()
if (DEFINED VPP_EXEC)
    execute_process(COMMAND "${VPP_EXEC}" run --compiler=cpp "${source}"
        WORKING_DIRECTORY "${VPP_OUTPUT_DIR}"
        RESULT_VARIABLE status OUTPUT_VARIABLE reference ERROR_VARIABLE errors TIMEOUT 120)
    if (NOT status STREQUAL "0" OR NOT reference STREQUAL output)
        message(FATAL_ERROR "System primitive Stage 0 parity failed: ${status}\n${reference}\n${errors}")
    endif()
endif()

# VM boundary errors are fatal error=2; the Stage 0 oracle must reject the
# same inputs. Public V++ wrappers keep their existing exception messages.
set(invalid_cases "máy ngủ mili giây(-1)" "máy ngủ mili giây(1000)"
    "máy ngủ mili giây(1.5)" "máy ngủ mili giây(\"x\")"
    "máy nguồn ngẫu nhiên các byte(0)" "máy nguồn ngẫu nhiên các byte(257)"
    "máy nguồn ngẫu nhiên các byte(1.5)" "máy nguồn ngẫu nhiên các byte(\"x\")")
set(index 0)
foreach(expression IN LISTS invalid_cases)
    math(EXPR index "${index} + 1")
    set(invalid_source "${VPP_OUTPUT_DIR}/system-invalid-${index}.vi")
    set(invalid_image "${VPP_OUTPUT_DIR}/system-invalid-${index}.vmb")
    file(WRITE "${invalid_source}" "hàm chính() { ${expression}; in \"unexpected\"; };\n")
    execute_process(COMMAND "${VPP_NATIVE_COMPILER}" vm-build "${invalid_source}" -o "${invalid_image}"
        RESULT_VARIABLE status OUTPUT_VARIABLE output ERROR_VARIABLE errors TIMEOUT 60)
    if (NOT status STREQUAL "0")
        message(FATAL_ERROR "Invalid system fixture compilation ${index}: ${status}\n${output}\n${errors}")
    endif()
    execute_process(COMMAND "${CMAKE_COMMAND}" -E env "PATH=" "${VPP_VM}" "${invalid_image}"
        RESULT_VARIABLE status OUTPUT_VARIABLE output ERROR_VARIABLE errors TIMEOUT 10)
    if (status STREQUAL "0" OR NOT errors MATCHES "error=2" OR NOT output STREQUAL "")
        message(FATAL_ERROR "VM accepted invalid system argument ${expression}: ${status}\n${output}\n${errors}")
    endif()
    if (DEFINED VPP_EXEC)
        execute_process(COMMAND "${VPP_EXEC}" run --compiler=cpp "${invalid_source}"
            RESULT_VARIABLE status OUTPUT_VARIABLE output ERROR_VARIABLE errors TIMEOUT 60)
        if (status STREQUAL "0" OR NOT output STREQUAL "")
            message(FATAL_ERROR "Stage 0 accepted invalid system argument ${expression}: ${status}\n${output}\n${errors}")
        endif()
    endif()
endforeach()

set(boundary "${VPP_OUTPUT_DIR}/native-system-boundary")
execute_process(COMMAND "${VPP_STAGE1}" build
    "${VPP_ROOT}/src/tests/fixtures/native_system_boundary.vi" -o "${boundary}"
    RESULT_VARIABLE status OUTPUT_VARIABLE output ERROR_VARIABLE errors TIMEOUT 120)
if (NOT status STREQUAL "0")
    message(FATAL_ERROR "Raw system boundary build failed: ${status}\n${output}\n${errors}")
endif()
execute_process(COMMAND "${CMAKE_COMMAND}" -E env "PATH=" "${boundary}"
    RESULT_VARIABLE status OUTPUT_VARIABLE output ERROR_VARIABLE errors TIMEOUT 10)
if (NOT status STREQUAL "0")
    message(FATAL_ERROR "Raw system boundary bounds check failed: ${status}\n${output}\n${errors}")
endif()
message(STATUS "V++/ASM clock, realtime, sleep, entropy and Stage 0 parity PASS")
