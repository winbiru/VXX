if(NOT DEFINED VPP_EXEC OR VPP_EXEC STREQUAL "")
    message(FATAL_ERROR "VPP_EXEC is required")
endif()
if(NOT DEFINED SAMPLE_ROOT OR SAMPLE_ROOT STREQUAL "")
    message(FATAL_ERROR "SAMPLE_ROOT is required")
endif()

function(run_sample)
    execute_process(
        COMMAND "${VPP_EXEC}" ${ARGN}
        WORKING_DIRECTORY "${SAMPLE_ROOT}"
        TIMEOUT 30
        RESULT_VARIABLE result
        OUTPUT_VARIABLE stdout
        ERROR_VARIABLE stderr)
    if(NOT result EQUAL 0)
        message(FATAL_ERROR
            "sample command failed (${result}): ${ARGN}\nstdout:\n${stdout}\nstderr:\n${stderr}")
    endif()
    set(LAST_STDOUT "${stdout}" PARENT_SCOPE)
endfunction()

foreach(required IN ITEMS
        "vpp.json"
        "src/hoa_don.vi"
        "src/main.vi"
        "tests/hoa_don.vi"
        "README.md")
    if(NOT EXISTS "${SAMPLE_ROOT}/${required}")
        message(FATAL_ERROR "sample hóa đơn thiếu ${required}")
    endif()
endforeach()

run_sample(dựng src/main.vi)
string(FIND "${LAST_STDOUT}" "Đã dựng thành công src/main.vi" built)
if(built EQUAL -1)
    message(FATAL_ERROR "sample không trả contract dựng mong đợi: ${LAST_STDOUT}")
endif()

run_sample(chạy src/main.vi)
string(FIND "${LAST_STDOUT}" "[IN] 125000" total)
if(total EQUAL -1)
    message(FATAL_ERROR "sample không tính đúng tổng thanh toán: ${LAST_STDOUT}")
endif()

run_sample(kiểm thử tests)
string(FIND "${LAST_STDOUT}" "Kiểm thử hoàn tất: 1/1 tệp đạt." tested)
if(tested EQUAL -1)
    message(FATAL_ERROR "sample không qua smoke test: ${LAST_STDOUT}")
endif()

message(STATUS "Sample project contract passed")
