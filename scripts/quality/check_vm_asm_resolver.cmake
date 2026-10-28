# Standalone VM numeric IP resolver requires no network or bind permission.
set(resolver_fixture "${VPP_ROOT}/src/tests/fixtures/vm_asm_resolver.vi")
set(resolver_image "${VPP_OUTPUT_DIR}/resolver.vmb")
execute_process(COMMAND "${VPP_NATIVE_COMPILER}" vm-build "${resolver_fixture}"
    -o "${resolver_image}"
    RESULT_VARIABLE status OUTPUT_VARIABLE output ERROR_VARIABLE errors TIMEOUT 120)
if(NOT status STREQUAL "0")
    message(FATAL_ERROR "Resolver fixture compile: ${status}\n${output}\n${errors}")
endif()
execute_process(COMMAND "${CMAKE_COMMAND}" -E env "PATH=" "${VPP_VM}" "${resolver_image}"
    RESULT_VARIABLE status OUTPUT_VARIABLE output ERROR_VARIABLE errors TIMEOUT 60)
if(NOT status STREQUAL "0" OR NOT output STREQUAL "VM ASM RESOLVER OK\n")
    message(FATAL_ERROR "Resolver VM/ASM numeric parity: ${status}\n${output}\n${errors}")
endif()
# A hostname still requires the full V++ DNS provider and must fail closed.
# Strict IP syntax prevents ambiguous legacy getaddrinfo abbreviations.
set(resolver_invalid_cases
    "máy bộ phân giải mở(\"example.com\", \"443\", 0)"
    "máy bộ phân giải mở(\"127.1\", \"80\", 0)"
    "máy bộ phân giải mở(\"1::2::3\", \"80\", 0)"
    "máy bộ phân giải mở(\"1:2:3:4:5:6:7\", \"80\", 0)"
    "máy bộ phân giải mở(\"1:2:3:4:5:6:7:8:9\", \"80\", 0)"
    "máy bộ phân giải mở(\":::1\", \"80\", 0)"
    "máy bộ phân giải mở(\"1:::2\", \"80\", 0)"
    "máy bộ phân giải mở(\"01.2.3.4\", \"80\", 0)"
    "máy bộ phân giải mở(chuỗi từ byte máy ảo([49, 0, 50, 55]), \"80\", 0)"
    "máy bộ phân giải mở(chuỗi từ byte máy ảo([255]), \"80\", 0)"
    "máy bộ phân giải mở(\"127.0.0.1\", \"0\", 0)"
    "máy bộ phân giải mở(\"127.0.0.1\", \"65536\", 0)"
    "máy bộ phân giải mở(\"127.0.0.1\", chuỗi từ byte máy ảo([56, 48, 0]), 0)"
    "máy bộ phân giải mở(\"127.0.0.1\", \"80\", 2)"
    "máy bộ phân giải tiếp(1342177281)"
    "máy bộ phân giải đóng(1342177281)")
set(resolver_case_index 0)
foreach(expression IN LISTS resolver_invalid_cases)
    math(EXPR resolver_case_index "${resolver_case_index} + 1")
    set(negative_source "${VPP_OUTPUT_DIR}/resolver-negative-${resolver_case_index}.vi")
    set(negative_image "${VPP_OUTPUT_DIR}/resolver-negative-${resolver_case_index}.vmb")
    file(WRITE "${negative_source}" "hàm chính() { ${expression}; };\n")
    execute_process(COMMAND "${VPP_NATIVE_COMPILER}" vm-build "${negative_source}"
        -o "${negative_image}"
        RESULT_VARIABLE code OUTPUT_VARIABLE output ERROR_VARIABLE errors TIMEOUT 120)
    if(NOT code STREQUAL "0")
        message(FATAL_ERROR "Resolver negative-${resolver_case_index} compile: ${code}\n${output}\n${errors}")
    endif()
    execute_process(COMMAND "${CMAKE_COMMAND}" -E env "PATH=" "${VPP_VM}" "${negative_image}"
        RESULT_VARIABLE code OUTPUT_VARIABLE output ERROR_VARIABLE errors TIMEOUT 30)
    if(resolver_case_index LESS 9)
        set(expected_error "error=22 opcode=203")
    else()
        set(expected_error "error=2")
    endif()
    if(code STREQUAL "0" OR NOT errors MATCHES "${expected_error}")
        message(FATAL_ERROR "Resolver negative-${resolver_case_index} mismatch: ${code}\n${output}\n${errors}")
    endif()
endforeach()
message(STATUS "V++ resolver 203..205: numeric IPv4/IPv6, monotonic tokens, strict invalid input PASS")
