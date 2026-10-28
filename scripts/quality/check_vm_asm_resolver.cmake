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
# Invalid hostnames must fail before any DNS transport is attempted.
# Strict IP syntax prevents ambiguous legacy getaddrinfo abbreviations.
set(resolver_invalid_cases
    "máy bộ phân giải mở(\"bad..example\", \"443\", 0)"
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

set(provider "${VPP_OUTPUT_DIR}/dns-provider.vmb")
execute_process(COMMAND "${VPP_NATIVE_COMPILER}" vm-build
    "${VPP_ROOT}/gói/máy ảo/nhà cung cấp DNS.vi" -o "${provider}"
    RESULT_VARIABLE status OUTPUT_VARIABLE output ERROR_VARIABLE errors TIMEOUT 180)
if(NOT status STREQUAL "0")
    message(FATAL_ERROR "V++ DNS provider compile: ${status}\n${output}\n${errors}")
endif()
set(dns_image "${VPP_OUTPUT_DIR}/resolver-dns.vmb")
execute_process(COMMAND "${VPP_NATIVE_COMPILER}" vm-build
    "${VPP_ROOT}/src/tests/fixtures/vm_asm_resolver_dns.vi" -o "${dns_image}"
    RESULT_VARIABLE status OUTPUT_VARIABLE output ERROR_VARIABLE errors TIMEOUT 120)
if(NOT status STREQUAL "0")
    message(FATAL_ERROR "DNS resolver guest compile: ${status}\n${output}\n${errors}")
endif()
find_program(resolver_python NAMES python3 REQUIRED)
execute_process(COMMAND "${resolver_python}" "${VPP_ROOT}/scripts/quality/check_vm_dns_provider.py"
    "${VPP_VM}" "${dns_image}" "${provider}" "${VPP_OUTPUT_DIR}" hosts
    RESULT_VARIABLE status OUTPUT_VARIABLE output ERROR_VARIABLE errors TIMEOUT 60)
if(NOT status STREQUAL "0")
    message(FATAL_ERROR "V++ DNS hosts/GC: ${status}\n${output}\n${errors}")
endif()
message(STATUS "${output}")
execute_process(COMMAND "${resolver_python}" "${VPP_ROOT}/scripts/quality/check_vm_dns_provider.py"
    "${VPP_VM}" "${dns_image}" "${provider}" "${VPP_OUTPUT_DIR}" wire
    RESULT_VARIABLE status OUTPUT_VARIABLE output ERROR_VARIABLE errors TIMEOUT 120)
if(status STREQUAL "77")
    message(STATUS "V++ DNS wire: sandbox bind denied, SKIP (hosts/GC still tested)")
elseif(NOT status STREQUAL "0")
    message(FATAL_ERROR "V++ DNS wire: ${status}\n${output}\n${errors}")
else()
    message(STATUS "${output}")
endif()
