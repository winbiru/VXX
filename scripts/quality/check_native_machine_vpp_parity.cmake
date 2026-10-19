# So sánh ba đường độc lập: fixture V++, C++ encoder gốc và CLI production.
# CLI production đã dùng V++ nên không thể dùng nó làm C++ oracle.
if (NOT DEFINED VPP_EXEC OR NOT DEFINED VPP_VI_SOURCE OR
    NOT DEFINED VPP_MACHINE_SOURCE OR NOT DEFINED VPP_CALL_SOURCE OR
    NOT DEFINED VPP_CPP_ORACLE OR NOT DEFINED VPP_OUTPUT_DIR)
    message(FATAL_ERROR "native machine V++ parity: thiếu input")
endif()

file(MAKE_DIRECTORY "${VPP_OUTPUT_DIR}")
execute_process(
    COMMAND "${VPP_EXEC}" "${VPP_VI_SOURCE}"
    RESULT_VARIABLE vi_status OUTPUT_VARIABLE vi_bytes ERROR_VARIABLE vi_error)
if (NOT vi_status EQUAL 0)
    message(FATAL_ERROR "AArch64 machine encoder V++ lỗi: ${vi_error} ${vi_bytes}")
endif()

execute_process(
    COMMAND "${VPP_CPP_ORACLE}" "${VPP_MACHINE_SOURCE}" "${VPP_CALL_SOURCE}"
    RESULT_VARIABLE oracle_status OUTPUT_VARIABLE oracle_bytes ERROR_VARIABLE oracle_error)
if (NOT oracle_status EQUAL 0)
    message(FATAL_ERROR "C++ machine oracle lỗi: ${oracle_error} ${oracle_bytes}")
endif()

foreach(target_spec IN ITEMS
    "aarch64-apple-darwin#aarch64#macos"
    "aarch64-linux-gnu#aarch64#linux"
    "x86_64-linux-gnu#x86_64#linux"
    "x86_64-windows-msvc#x86_64#windows")
    string(REPLACE "#" ";" parts "${target_spec}")
    list(GET parts 0 target)
    list(GET parts 1 architecture)
    list(GET parts 2 os)
    foreach(function_name IN ITEMS tru_dao tong_bon dieu_kien max_i32 tong goi_bon goi_muoi tong_muoi)
        string(REGEX MATCH "(^|\n)${function_name}#${architecture}#${os}:([0-9a-f]+)" matched "${vi_bytes}")
        if (NOT matched)
            message(FATAL_ERROR "V++ fixture thiếu ${target}/${function_name}: ${vi_bytes}")
        endif()
        set(vpp_hex "${CMAKE_MATCH_2}")
        string(REGEX MATCH "(^|\n)${function_name}#${architecture}#${os}:([0-9a-f]+)" reference "${oracle_bytes}")
        if (NOT reference)
            message(FATAL_ERROR "C++ oracle thiếu ${target}/${function_name}: ${oracle_bytes}")
        endif()
        set(reference_hex "${CMAKE_MATCH_2}")
        if (NOT reference_hex STREQUAL vpp_hex)
            message(FATAL_ERROR "${target}/${function_name}: encoder C++ khác V++ fixture\nC++: ${reference_hex}\nV++: ${vpp_hex}")
        endif()
        if (function_name STREQUAL "goi_bon" OR function_name STREQUAL "goi_muoi")
            if (function_name STREQUAL "goi_bon")
                set(callee "bon")
            else()
                set(callee "tong_muoi")
            endif()
            string(REGEX MATCH "(^|\n)R#${function_name}#${architecture}#${os}:([0-9]+):${callee}(\n|$)" reloc "${oracle_bytes}")
            if (NOT reloc)
                message(FATAL_ERROR "C++ oracle thiếu relocation ${callee}: ${target}/${function_name}")
            endif()
        endif()
        set(raw "${VPP_OUTPUT_DIR}/${target}-${function_name}.bin")
        if (function_name STREQUAL "goi_bon" OR function_name STREQUAL "goi_muoi")
            # --emit-raw từ chối CALL chưa liên kết. Kiểm tra toàn bộ byte
            # section code hiện diện liên tục trong ELF/Mach-O/COFF object.
            set(raw "${VPP_OUTPUT_DIR}/${target}-${function_name}.o")
            execute_process(
                COMMAND "${VPP_EXEC}" build --target "${target}" --emit-obj
                    "${function_name}" "${VPP_CALL_SOURCE}" "${raw}"
                RESULT_VARIABLE cli_status OUTPUT_VARIABLE cli_output ERROR_VARIABLE cli_error)
        else()
            if (function_name STREQUAL "tong_muoi")
                set(function_source "${VPP_CALL_SOURCE}")
            else()
                set(function_source "${VPP_MACHINE_SOURCE}")
            endif()
            execute_process(
                COMMAND "${VPP_EXEC}" build --target "${target}" --emit-raw
                    "${function_name}" "${function_source}" "${raw}"
                RESULT_VARIABLE cli_status OUTPUT_VARIABLE cli_output ERROR_VARIABLE cli_error)
        endif()
        if (NOT cli_status EQUAL 0)
            message(FATAL_ERROR "V++ production CLI lỗi ${target}/${function_name}: ${cli_error}")
        endif()
        file(READ "${raw}" cli_hex HEX)
        if (function_name STREQUAL "goi_bon" OR function_name STREQUAL "goi_muoi")
            string(FIND "${cli_hex}" "${vpp_hex}" offset)
            if (offset EQUAL -1)
                message(FATAL_ERROR "${target}/${function_name}: byte V++ khác CLI production object")
            endif()
        elseif (NOT cli_hex STREQUAL vpp_hex)
            message(FATAL_ERROR "${target}/${function_name}: V++ fixture khác CLI production\nCLI: ${cli_hex}\nV++: ${vpp_hex}")
        endif()
    endforeach()
endforeach()
message(STATUS "Native AArch64/x86_64 independent C++/V++/CLI byte parity: 32/32")
