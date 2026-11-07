foreach(required IN ITEMS VPP_EXEC VPP_CPP_ORACLE VPP_NATIVE_COMPILER VPP_ROOT VPP_OUTPUT_DIR)
    if (NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "Native formatter: missing ${required}")
    endif()
endforeach()
file(MAKE_DIRECTORY "${VPP_OUTPUT_DIR}")

function(format_with label executable source)
    execute_process(
        COMMAND "${CMAKE_COMMAND}" -E env "VPP_HOME=${VPP_ROOT}"
            "${executable}" ${ARGN} "${source}"
        RESULT_VARIABLE status OUTPUT_VARIABLE result ERROR_VARIABLE errors
        TIMEOUT 90)
    if (NOT status STREQUAL "0")
        message(FATAL_ERROR "Native formatter ${label} failed (${status}): ${errors}")
    endif()
    set(${label} "${result}" PARENT_SCOPE)
endfunction()

file(WRITE "${VPP_OUTPUT_DIR}/format-1.vi" "")
file(WRITE "${VPP_OUTPUT_DIR}/format-2.vi" "in 1;")
file(WRITE "${VPP_OUTPUT_DIR}/format-3.vi" "hàm chính(){in \"Xin chào Việt Nam 🌿\";}\n")
file(WRITE "${VPP_OUTPUT_DIR}/format-4.vi"
    "// bình luận tiếng Việt\nhàm chính(){/* khối */in \"a // b\";}\n")
file(WRITE "${VPP_OUTPUT_DIR}/format-5.vi"
    "hàm f(x){nếu(x>1){in x;} hoặc nếu(x==1){in 1;} hoặc{in 0;};};")
file(WRITE "${VPP_OUTPUT_DIR}/format-6.vi"
    "hàm f(){lặp(i=0;i<3;i++){in [1,2,3][i];};};")
file(WRITE "${VPP_OUTPUT_DIR}/format-7.vi"
    "hàm f(){in \"dấu /* không là comment */\";/*nhiều\ndòng*/in 3;}")
file(WRITE "${VPP_OUTPUT_DIR}/format-8.vi" "  \n\n")
file(WRITE "${VPP_OUTPUT_DIR}/format-9.vi"
    "// bình luận CRLF\r\nhàm chính(){in \"chào 🌿\";};\r\n")
file(WRITE "${VPP_OUTPUT_DIR}/format-10.vi"
    "/* nhiều\r\ndòng */\r\nhàm chính(){in \"literal \\\\r\\\\n\";};\r\n")
file(WRITE "${VPP_OUTPUT_DIR}/format-11.vi" "// comment at EOF")

foreach(index RANGE 1 11)
    set(source "${VPP_OUTPUT_DIR}/format-${index}.vi")
    file(SHA256 "${source}" original_hash)

    format_with(cpp "${VPP_CPP_ORACLE}" "${source}")
    format_with(native "${VPP_NATIVE_COMPILER}" "${source}" format)
    format_with(cli "${VPP_EXEC}" "${source}" --định-dạng)
    if (NOT cpp STREQUAL native OR NOT native STREQUAL cli)
        message(FATAL_ERROR "Native formatter parity mismatch on fixture ${index}\n"
            "Stage 0: [${cpp}]\nNative: [${native}]\nCLI: [${cli}]")
    endif()

    set(formatted "${VPP_OUTPUT_DIR}/formatted-${index}.vi")
    file(WRITE "${formatted}" "${native}")
    format_with(twice "${VPP_NATIVE_COMPILER}" "${formatted}" format)
    if (NOT twice STREQUAL native)
        message(FATAL_ERROR "Native formatter is not idempotent on fixture ${index}")
    endif()
    file(SHA256 "${source}" final_hash)
    if (NOT final_hash STREQUAL original_hash)
        message(FATAL_ERROR "Native formatter changed source on fixture ${index}")
    endif()
endforeach()

# A trailing unmatched quote must fail consistently, without overwriting input.
set(invalid "${VPP_OUTPUT_DIR}/invalid.vi")
file(WRITE "${invalid}" "in \"chưa đóng;\n")
file(SHA256 "${invalid}" invalid_hash)
foreach(command IN ITEMS cpp native)
    if (command STREQUAL "cpp")
        set(executable "${VPP_CPP_ORACLE}")
        set(arguments "")
    else()
        set(executable "${VPP_NATIVE_COMPILER}")
        set(arguments "format")
    endif()
    execute_process(
        COMMAND "${CMAKE_COMMAND}" -E env "VPP_HOME=${VPP_ROOT}"
            "${executable}" ${arguments} "${invalid}"
        RESULT_VARIABLE status OUTPUT_VARIABLE output ERROR_VARIABLE errors
        TIMEOUT 90)
    if (status STREQUAL "0")
        message(FATAL_ERROR "Native formatter ${command} accepted unterminated literal")
    endif()
endforeach()
file(SHA256 "${invalid}" invalid_final_hash)
if (NOT invalid_hash STREQUAL invalid_final_hash)
    message(FATAL_ERROR "Native formatter changed invalid input")
endif()

# CLI --check does not write; --write changes the file once, and a second
# check must succeed. Keep the assertions independent of summary language.
set(writable "${VPP_OUTPUT_DIR}/write-mode.vi")
file(WRITE "${writable}" "hàm chính(){in 1;};")
file(SHA256 "${writable}" before_check)
execute_process(
    COMMAND "${CMAKE_COMMAND}" -E env "VPP_HOME=${VPP_ROOT}"
        "${VPP_EXEC}" --định-dạng "${writable}" --kiểm-tra
    RESULT_VARIABLE check_status OUTPUT_VARIABLE check_output ERROR_VARIABLE check_error)
if (check_status STREQUAL "0")
    message(FATAL_ERROR "Native formatter --check missed unformatted source")
endif()
file(SHA256 "${writable}" after_check)
if (NOT after_check STREQUAL before_check)
    message(FATAL_ERROR "Native formatter --check modified the source")
endif()
execute_process(
    COMMAND "${CMAKE_COMMAND}" -E env "VPP_HOME=${VPP_ROOT}"
        "${VPP_EXEC}" --định-dạng "${writable}" --ghi-tệp
    RESULT_VARIABLE write_status OUTPUT_VARIABLE write_output ERROR_VARIABLE write_error)
if (NOT write_status STREQUAL "0")
    message(FATAL_ERROR "Native formatter --write failed: ${write_error}")
endif()
execute_process(
    COMMAND "${CMAKE_COMMAND}" -E env "VPP_HOME=${VPP_ROOT}"
        "${VPP_EXEC}" --định-dạng "${writable}" --kiểm-tra
    RESULT_VARIABLE final_status OUTPUT_VARIABLE final_output ERROR_VARIABLE final_error)
if (NOT final_status STREQUAL "0")
    message(FATAL_ERROR "Native formatter --check failed after --write: ${final_error}")
endif()
message(STATUS "Native V++ formatter: parity, unicode, comments, idempotence and CLI modes PASS")
