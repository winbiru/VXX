foreach(required IN ITEMS VPP_NATIVE_CLI VPP_ROOT VPP_OUTPUT_DIR)
    if (NOT DEFINED ${required})
        message(FATAL_ERROR "Thiếu tham số kiểm thử closure: ${required}")
    endif()
endforeach()
file(MAKE_DIRECTORY "${VPP_OUTPUT_DIR}")

function(check_closure name source expected)
    set(executable "${VPP_OUTPUT_DIR}/${name}")
    execute_process(COMMAND "${CMAKE_COMMAND}" -E env "VPP_HOME=${VPP_ROOT}"
        "${VPP_NATIVE_CLI}" build "${VPP_ROOT}/${source}" -o "${executable}"
        WORKING_DIRECTORY "${VPP_ROOT}"
        RESULT_VARIABLE status OUTPUT_VARIABLE output ERROR_VARIABLE errors TIMEOUT 120)
    if (NOT status STREQUAL "0")
        message(FATAL_ERROR "Compiler tự chủ không biên dịch được ${name}: ${status}\n${output}\n${errors}")
    endif()
    execute_process(COMMAND "${CMAKE_COMMAND}" -E env "VPP_HOME=${VPP_ROOT}" "${executable}"
        WORKING_DIRECTORY "${VPP_ROOT}"
        RESULT_VARIABLE status OUTPUT_VARIABLE output ERROR_VARIABLE errors TIMEOUT 30)
    file(READ "${VPP_ROOT}/${expected}" wanted)
    if (NOT status STREQUAL "0" OR NOT output STREQUAL wanted)
        message(FATAL_ERROR "Closure ${name} sai kết quả (${status}):\n${output}\n${errors}")
    endif()
endfunction()

check_closure(capture src/tests/kiem_tra_closure_capture.vi
    src/tests/expected/kiem_tra_closure_capture.expected)
check_closure(scopes src/tests/fixtures/anonymous_functions/entry.vi
    src/tests/expected/kiem_tra_ham_an_danh.expected)
check_closure(modules src/tests/fixtures/anonymous_modules/entry.vi
    src/tests/expected/kiem_tra_ham_an_danh_module.expected)
check_closure(oop src/tests/kiem_tra_native_oop.vi
    src/tests/expected/kiem_tra_native_oop.expected)

# Closure không được dùng vòng lặp ngoài làm đích cho thoát hoặc bỏ qua.
set(invalid "${VPP_OUTPUT_DIR}/invalid.vi")
set(preserved "${VPP_OUTPUT_DIR}/preserved")
foreach(keyword IN ITEMS "thoát" "bỏ qua")
    file(WRITE "${invalid}" "hàm chính() { lặp(i = 0; i < 1; i++) { biến f = hàm() { ${keyword}; }; }; };\n")
    file(WRITE "${preserved}" "giữ nguyên")
    execute_process(COMMAND "${CMAKE_COMMAND}" -E env "VPP_HOME=${VPP_ROOT}"
        "${VPP_NATIVE_CLI}" build "${invalid}" -o "${preserved}"
        RESULT_VARIABLE status OUTPUT_VARIABLE output ERROR_VARIABLE errors TIMEOUT 30)
    file(READ "${preserved}" content)
    if (status STREQUAL "0" OR NOT content STREQUAL "giữ nguyên")
        message(FATAL_ERROR "Compiler tự chủ chấp nhận '${keyword}' ngoài phạm vi closure hoặc ghi đè đầu ra")
    endif()
endforeach()
message(STATUS "Closure qua compiler tự chủ: bắt biến, phạm vi, mô-đun và OOP đều đạt")
