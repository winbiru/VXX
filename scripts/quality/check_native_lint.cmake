foreach(required IN ITEMS VPP_NATIVE_COMPILER VPP_ROOT VPP_OUTPUT_DIR)
    if (NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "Native lint: missing ${required}")
    endif()
endforeach()
file(MAKE_DIRECTORY "${VPP_OUTPUT_DIR}")
file(WRITE "${VPP_OUTPUT_DIR}/valid.vi" "hàm chính() { in \"chào 🌿\"; };\n")
file(WRITE "${VPP_OUTPUT_DIR}/syntax.vi" "hàm f( {\n")
file(WRITE "${VPP_OUTPUT_DIR}/semantic.vi" "hàm f() {};\nhàm f() {};\n")
file(WRITE "${VPP_OUTPUT_DIR}/semantic_utf8.vi" "in \"chào 🌿\";\n  hàm f() {};\n  hàm f() {};\n")
file(WRITE "${VPP_OUTPUT_DIR}/duplicate_class.vi" "lớp Dữ Liệu {};\nlớp Dữ Liệu {};\n")
file(WRITE "${VPP_OUTPUT_DIR}/imported.vi" "hàm hỗ trợ() { trả về 1; };\n")
file(WRITE "${VPP_OUTPUT_DIR}/import_valid.vi" "nhập imported;\nhàm chính() { in 1; };\n")
file(WRITE "${VPP_OUTPUT_DIR}/import_missing.vi" "nhập không tồn tại;\n")
file(WRITE "${VPP_OUTPUT_DIR}/import_cycle.vi" "nhập import_cycle_child;\n")
file(WRITE "${VPP_OUTPUT_DIR}/import_cycle_child.vi" "nhập import_cycle;\n")
file(WRITE "${VPP_OUTPUT_DIR}/import_syntax.vi" "nhập import_bad_child;\n")
file(WRITE "${VPP_OUTPUT_DIR}/import_bad_child.vi" "hàm f( {\n")
file(MAKE_DIRECTORY "${VPP_OUTPUT_DIR}/lint-origin" "${VPP_OUTPUT_DIR}/lint-buffer")
file(WRITE "${VPP_OUTPUT_DIR}/lint-origin/helper.vi" "hàm phụ() { trả về 1; };\n")
file(WRITE "${VPP_OUTPUT_DIR}/lint-buffer/snapshot.vi" "nhập helper;\nhàm chính() { in 1; };\n")

function(lint label name expected_status expected_pattern)
    set(path "${VPP_OUTPUT_DIR}/${name}.vi")
    file(SHA256 "${path}" initial)
    execute_process(
        COMMAND "${CMAKE_COMMAND}" -E env "VPP_HOME=${VPP_ROOT}"
            "${VPP_NATIVE_COMPILER}" lint "${path}"
        RESULT_VARIABLE status OUTPUT_VARIABLE output ERROR_VARIABLE errors
        TIMEOUT 90)
    if (NOT status STREQUAL "${expected_status}")
        message(FATAL_ERROR "Native lint ${label} unexpected status ${status}: ${output} ${errors}")
    endif()
    if (NOT "${output}" MATCHES "${expected_pattern}")
        message(FATAL_ERROR "Native lint ${label} unexpected diagnostic: ${output}")
    endif()
    file(SHA256 "${path}" final)
    if (NOT initial STREQUAL final)
        message(FATAL_ERROR "Native lint ${label} modified input")
    endif()
endfunction()

lint(valid valid 0 "^$")
lint(syntax syntax 1 "1:9: lỗi: biên dịch biểu thức:")
lint(semantic semantic 1 "2:1: lỗi: biên dịch ngữ nghĩa: trùng khai báo hàm")
lint(semantic_utf8 semantic_utf8 1 "3:3: lỗi: biên dịch ngữ nghĩa: trùng khai báo hàm")
lint(duplicate_class duplicate_class 1 "2:1: lỗi: biên dịch ngữ nghĩa: trùng khai báo kiểu")
lint(import_valid import_valid 0 "^$")
lint(import_missing import_missing 1 "không tìm thấy import")
lint(import_cycle import_cycle 1 "import vòng")
lint(import_syntax import_syntax 1 "import_bad_child.vi.*tại dòng 1, cột 9")

# Snapshot nằm ngoài thư mục import; chỉ --base mới cho phép resolve
# theo thư mục nguồn thực khi LSP chưa lưu buffer xuống đĩa.
execute_process(
    COMMAND "${CMAKE_COMMAND}" -E env "VPP_HOME=${VPP_ROOT}"
        "${VPP_NATIVE_COMPILER}" lint "${VPP_OUTPUT_DIR}/lint-buffer/snapshot.vi"
    RESULT_VARIABLE missing_base_status OUTPUT_VARIABLE missing_base_output
    ERROR_VARIABLE missing_base_error TIMEOUT 90)
if (NOT missing_base_status STREQUAL "1" OR
    NOT "${missing_base_output}" MATCHES "không tìm thấy import")
    message(FATAL_ERROR "Native lint snapshot without base unexpectedly resolved: ${missing_base_status} ${missing_base_output} ${missing_base_error}")
endif()
execute_process(
    COMMAND "${CMAKE_COMMAND}" -E env "VPP_HOME=${VPP_ROOT}"
        "${VPP_NATIVE_COMPILER}" lint "${VPP_OUTPUT_DIR}/lint-buffer/snapshot.vi"
        --base "${VPP_OUTPUT_DIR}/lint-origin"
    RESULT_VARIABLE with_base_status OUTPUT_VARIABLE with_base_output
    ERROR_VARIABLE with_base_error TIMEOUT 90)
if (NOT with_base_status STREQUAL "0" OR NOT "${with_base_output}" STREQUAL "")
    message(FATAL_ERROR "Native lint snapshot --base failed: ${with_base_status} ${with_base_output} ${with_base_error}")
endif()

if (DEFINED VPP_CLI AND NOT "${VPP_CLI}" STREQUAL "")
    function(lint_cli label expected_status expected_pattern)
        execute_process(
            COMMAND "${CMAKE_COMMAND}" -E env
                "VPP_HOME=${VPP_ROOT}" "VPP_NATIVE_COMPILER=${VPP_NATIVE_COMPILER}"
                "${VPP_CLI}" --soát-lỗi ${ARGN}
            RESULT_VARIABLE status OUTPUT_VARIABLE output ERROR_VARIABLE errors
            TIMEOUT 90)
        if (NOT status STREQUAL "${expected_status}" OR
            NOT "${output}${errors}" MATCHES "${expected_pattern}")
            message(FATAL_ERROR "CLI lint ${label}: status=${status}, output=${output}, errors=${errors}")
        endif()
    endfunction()
    lint_cli(native_valid 0 "1 tệp, 0 lỗi, 0 cảnh báo" "${VPP_OUTPUT_DIR}/valid.vi")
    lint_cli(native_duplicate 1 "semantic.vi:2:1: lỗi: biên dịch ngữ nghĩa" "${VPP_OUTPUT_DIR}/semantic.vi")
    lint_cli(native_import 1 "không tìm thấy import" "${VPP_OUTPUT_DIR}/import_missing.vi")
    lint_cli(explicit_vpp 1 "semantic.vi:2:1: lỗi: biên dịch ngữ nghĩa"
        --compiler=vpp "${VPP_OUTPUT_DIR}/semantic.vi")
    lint_cli(explicit_cpp 1 "semantic.vi:2:1: lỗi: Khai báo"
        --compiler=cpp "${VPP_OUTPUT_DIR}/semantic.vi")
    lint_cli(invalid_option 1 "Cú pháp: vpp --soát-lỗi" --compiler=unknown
        "${VPP_OUTPUT_DIR}/valid.vi")
endif()
message(STATUS "Native V++ lint: syntax, span, semantic, unicode, recursive import and read-only checks PASS")
