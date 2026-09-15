if(NOT DEFINED VPP_EXEC OR VPP_EXEC STREQUAL "")
    message(FATAL_ERROR "VPP_EXEC is required")
endif()
if(NOT DEFINED TEST_ROOT OR TEST_ROOT STREQUAL "")
    message(FATAL_ERROR "TEST_ROOT is required")
endif()

file(REMOVE_RECURSE "${TEST_ROOT}")
file(MAKE_DIRECTORY "${TEST_ROOT}/du-an/tests" "${TEST_ROOT}/alias-new")

function(run_vpp working_directory)
    execute_process(
        COMMAND "${VPP_EXEC}" ${ARGN}
        WORKING_DIRECTORY "${working_directory}"
        RESULT_VARIABLE result
        OUTPUT_VARIABLE stdout
        ERROR_VARIABLE stderr)
    if(NOT result EQUAL 0)
        message(FATAL_ERROR
            "vpp command failed (${result}): ${ARGN}\nstdout:\n${stdout}\nstderr:\n${stderr}")
    endif()
    set(LAST_STDOUT "${stdout}" PARENT_SCOPE)
    set(LAST_STDERR "${stderr}" PARENT_SCOPE)
endfunction()

function(run_vpp_expect_failure working_directory)
    execute_process(
        COMMAND "${VPP_EXEC}" ${ARGN}
        WORKING_DIRECTORY "${working_directory}"
        RESULT_VARIABLE result
        OUTPUT_VARIABLE stdout
        ERROR_VARIABLE stderr)
    if(result EQUAL 0)
        message(FATAL_ERROR
            "vpp command unexpectedly succeeded: ${ARGN}\nstdout:\n${stdout}\nstderr:\n${stderr}")
    endif()
    set(LAST_STDOUT "${stdout}" PARENT_SCOPE)
    set(LAST_STDERR "${stderr}" PARENT_SCOPE)
endfunction()

run_vpp("${TEST_ROOT}" giúp đỡ)
foreach(expected IN ITEMS
        "vpp chạy <file.vi>"
        "vpp kiểm thử [<tệp-hoặc-thư-mục>]"
        "vpp dựng <file.vi>"
        "vpp --soát-lỗi <tệp-hoặc-thư-mục>"
        "vpp --định-dạng <tệp-hoặc-thư-mục> [--ghi-tệp | --kiểm-tra]"
        "vpp khởi tạo [tên-dự-án]"
        "vpp khởi tạo ứng dụng <tên-dự-án>"
        "vpp gói cài đặt")
    string(FIND "${LAST_STDOUT}" "${expected}" found)
    if(found EQUAL -1)
        message(FATAL_ERROR "help thiếu contract CLI tiếng Việt: ${expected}\n${LAST_STDOUT}")
    endif()
endforeach()

run_vpp("${TEST_ROOT}/du-an" khởi tạo demo-viet)
if(NOT EXISTS "${TEST_ROOT}/du-an/vpp.json")
    message(FATAL_ERROR "khởi tạo không tạo vpp.json")
endif()

file(WRITE "${TEST_ROOT}/du-an/main.vi"
    "hàm main() { in 42; }\n")
file(WRITE "${TEST_ROOT}/du-an/tests/a.vi"
    "hàm main() { in \"A\"; }\n")
file(WRITE "${TEST_ROOT}/du-an/tests/b.vi"
    "hàm main() { in \"B\"; }\n")

run_vpp("${TEST_ROOT}/du-an" dựng main.vi)
string(FIND "${LAST_STDOUT}" "Đã dựng thành công main.vi" build_vi)
if(build_vi EQUAL -1)
    message(FATAL_ERROR "lệnh dựng không trả thông báo chuẩn tiếng Việt: ${LAST_STDOUT}")
endif()

run_vpp("${TEST_ROOT}/du-an" build main.vi)
string(FIND "${LAST_STDOUT}" "Đã dựng thành công main.vi" build_alias)
if(build_alias EQUAL -1)
    message(FATAL_ERROR "alias build không giữ contract dựng: ${LAST_STDOUT}")
endif()

run_vpp("${TEST_ROOT}/du-an" chạy main.vi)
if(NOT LAST_STDOUT STREQUAL "[IN] 42\n")
    message(FATAL_ERROR "lệnh chạy cho output không mong đợi: ${LAST_STDOUT}")
endif()

run_vpp("${TEST_ROOT}/du-an" run main.vi)
if(NOT LAST_STDOUT STREQUAL "[IN] 42\n")
    message(FATAL_ERROR "alias run không giữ contract chạy: ${LAST_STDOUT}")
endif()

run_vpp("${TEST_ROOT}/du-an" kiểm thử tests)
string(FIND "${LAST_STDOUT}" "Kiểm thử hoàn tất: 2/2 tệp đạt." test_vi)
if(test_vi EQUAL -1)
    message(FATAL_ERROR "lệnh kiểm thử không trả summary chuẩn: ${LAST_STDOUT}")
endif()

run_vpp("${TEST_ROOT}/du-an" test tests)
string(FIND "${LAST_STDOUT}" "Kiểm thử hoàn tất: 2/2 tệp đạt." test_alias)
if(test_alias EQUAL -1)
    message(FATAL_ERROR "alias test không giữ contract kiểm thử: ${LAST_STDOUT}")
endif()

run_vpp("${TEST_ROOT}/du-an" --soát-lỗi main.vi)
string(FIND "${LAST_STDOUT}" "Soát lỗi hoàn tất: 1 tệp, 0 lỗi, 0 cảnh báo." lint_ok)
if(lint_ok EQUAL -1)
    message(FATAL_ERROR "--soát-lỗi không trả summary ổn định: ${LAST_STDOUT}")
endif()

file(MAKE_DIRECTORY "${TEST_ROOT}/du-an/soat-import")
file(WRITE "${TEST_ROOT}/du-an/soat-import/module.vi"
    "hàm ping() { trả về 7; }\n")
file(WRITE "${TEST_ROOT}/du-an/soat-import/main.vi"
    "nhập module.vi;\nhàm main() { in ping(); }\n")
run_vpp("${TEST_ROOT}/du-an" --soát-lỗi soat-import/main.vi)
string(FIND "${LAST_STDOUT}" "1 tệp, 0 lỗi" lint_import_ok)
if(lint_import_ok EQUAL -1)
    message(FATAL_ERROR "--soát-lỗi không resolve import tương đối theo tệp nguồn: ${LAST_STDOUT}")
endif()

file(WRITE "${TEST_ROOT}/du-an/can-dinh-dang.vi"
    "// giữ comment\nhàm main(){x=1+2;nếu(x>1){in \"Việt Nam\";}}\n")
run_vpp_expect_failure("${TEST_ROOT}/du-an" --định-dạng can-dinh-dang.vi --kiểm-tra)
string(FIND "${LAST_STDERR}" "Chưa đúng định dạng: can-dinh-dang.vi" format_check_failed)
if(format_check_failed EQUAL -1)
    message(FATAL_ERROR "--kiểm-tra không phát hiện file chưa định dạng: ${LAST_STDERR}")
endif()
run_vpp("${TEST_ROOT}/du-an" --định-dạng can-dinh-dang.vi --ghi-tệp)
file(READ "${TEST_ROOT}/du-an/can-dinh-dang.vi" formatted_source)
string(FIND "${formatted_source}" "// giữ comment" preserved_comment)
string(FIND "${formatted_source}" "nếu (x > 1)" formatted_if)
if(preserved_comment EQUAL -1 OR formatted_if EQUAL -1)
    message(FATAL_ERROR "formatter không giữ comment/spacing chuẩn:\n${formatted_source}")
endif()
run_vpp("${TEST_ROOT}/du-an" --định-dạng can-dinh-dang.vi --kiểm-tra)
string(FIND "${LAST_STDOUT}" "1 tệp, 0 tệp thay đổi" format_check_passed)
if(format_check_passed EQUAL -1)
    message(FATAL_ERROR "formatter không idempotent qua CLI: ${LAST_STDOUT}")
endif()

file(MAKE_DIRECTORY "${TEST_ROOT}/du-an/soat-loi")
file(WRITE "${TEST_ROOT}/du-an/soat-loi/tot.vi" "hàm main() { in 1; }\n")
file(WRITE "${TEST_ROOT}/du-an/soat-loi/loi.vi" "in 1;\n}\n")
run_vpp_expect_failure("${TEST_ROOT}/du-an" --soát-lỗi soat-loi)
string(FIND "${LAST_STDERR}" "loi.vi:2:1: lỗi:" lint_location)
string(FIND "${LAST_STDOUT}" "Soát lỗi hoàn tất: 2 tệp, 1 lỗi" lint_summary)
if(lint_location EQUAL -1 OR lint_summary EQUAL -1)
    message(FATAL_ERROR
        "linter directory contract không có vị trí/summary đúng:\nstdout=${LAST_STDOUT}\nstderr=${LAST_STDERR}")
endif()

run_vpp("${TEST_ROOT}/du-an" gói danh sách)
string(FIND "${LAST_STDOUT}" "Chưa có gói nào được cài." package_list)
if(package_list EQUAL -1)
    message(FATAL_ERROR "gói danh sách không dùng output tiếng Việt chuẩn: ${LAST_STDOUT}")
endif()

run_vpp("${TEST_ROOT}/du-an" gói khóa)
run_vpp("${TEST_ROOT}/du-an" gói phục hồi --ngoại-tuyến)
string(FIND "${LAST_STDOUT}" "Đã phục hồi 0 gói từ tệp khóa." package_restore)
if(package_restore EQUAL -1)
    message(FATAL_ERROR "cờ --ngoại-tuyến không hoạt động với gói phục hồi: ${LAST_STDOUT}")
endif()

run_vpp("${TEST_ROOT}/alias-new" new alias-demo)
if(NOT EXISTS "${TEST_ROOT}/alias-new/vpp.json")
    message(FATAL_ERROR "alias new không giữ contract khởi tạo")
endif()

run_vpp("${TEST_ROOT}/du-an" chẩn đoán)
string(FIND "${LAST_STDOUT}" "Chẩn đoán V++ CLI" doctor_vi)
if(doctor_vi EQUAL -1)
    message(FATAL_ERROR "lệnh chẩn đoán không hoạt động: ${LAST_STDOUT}")
endif()

run_vpp("${TEST_ROOT}" khởi tạo ứng dụng mau-ung-dung)
foreach(expected_file IN ITEMS
        "vpp.json"
        "src/main.vi"
        "tests/smoke.vi"
        "README.md"
        ".gitignore")
    if(NOT EXISTS "${TEST_ROOT}/mau-ung-dung/${expected_file}")
        message(FATAL_ERROR "template ứng dụng thiếu ${expected_file}")
    endif()
endforeach()
run_vpp("${TEST_ROOT}/mau-ung-dung" dựng src/main.vi)
string(FIND "${LAST_STDOUT}" "Đã dựng thành công src/main.vi" app_build)
if(app_build EQUAL -1)
    message(FATAL_ERROR "template ứng dụng không dựng được: ${LAST_STDOUT}")
endif()
run_vpp("${TEST_ROOT}/mau-ung-dung" chạy src/main.vi)
string(FIND "${LAST_STDOUT}" "Xin chào từ V++" app_run)
if(app_run EQUAL -1)
    message(FATAL_ERROR "template ứng dụng không chạy được: ${LAST_STDOUT}")
endif()
run_vpp("${TEST_ROOT}/mau-ung-dung" kiểm thử tests)
string(FIND "${LAST_STDOUT}" "Kiểm thử hoàn tất: 1/1 tệp đạt." app_test)
if(app_test EQUAL -1)
    message(FATAL_ERROR "template ứng dụng không qua kiểm thử: ${LAST_STDOUT}")
endif()

message(STATUS "Vietnamese CLI contract passed")
