# AST/IR source inspections must use the native V++ frontend when requested.
# Stage 0 has a distinct IR schema, so compare behavior and structure instead
# of expecting identical output bytes across compiler generations.
foreach(required IN ITEMS VPP_EXEC VPP_NATIVE_COMPILER VPP_ROOT VPP_OUTPUT_DIR)
    if (NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "Native AST/IR: missing ${required}")
    endif()
endforeach()
if (NOT EXISTS "${VPP_NATIVE_COMPILER}")
    message(FATAL_ERROR "Native AST/IR: native compiler missing")
endif()
file(MAKE_DIRECTORY "${VPP_OUTPUT_DIR}")

set(source "${VPP_OUTPUT_DIR}/dump-fixture.vi")
file(WRITE "${source}"
    "hàm cộng(a, b) { trả về a + b; };\n"
    "hàm chính() { in cộng(2, 3); };\n")
file(SHA256 "${source}" original_hash)

function(run_dump label mode)
    execute_process(
        COMMAND "${CMAKE_COMMAND}" -E env "VPP_HOME=${VPP_ROOT}"
            "VPP_NATIVE_COMPILER=${VPP_NATIVE_COMPILER}" ${ARGN}
        WORKING_DIRECTORY "${VPP_OUTPUT_DIR}"
        RESULT_VARIABLE status OUTPUT_VARIABLE output ERROR_VARIABLE errors
        TIMEOUT 180)
    if (NOT status STREQUAL "0" OR output STREQUAL "")
        message(FATAL_ERROR "Native AST/IR ${label} (${mode}) failed (${status}): ${errors}")
    endif()
    set(${label}_output "${output}" PARENT_SCOPE)
endfunction()

foreach(mode IN ITEMS ast ir)
    run_dump(cpp_${mode} "${mode}" "${VPP_EXEC}" "--dump-${mode}" --compiler=cpp "${source}")
    run_dump(vpp_${mode} "${mode}" "${VPP_EXEC}" "--dump-${mode}" --compiler=vpp "${source}")
    run_dump(default_${mode} "${mode}" "${VPP_EXEC}" "--dump-${mode}" "${source}")
    run_dump(native_${mode} "${mode}" "${VPP_NATIVE_COMPILER}" "dump-${mode}" "${source}")
    if (NOT vpp_${mode}_output STREQUAL default_${mode}_output OR
        NOT vpp_${mode}_output STREQUAL native_${mode}_output)
        message(FATAL_ERROR "Native AST/IR ${mode}: native, default and direct outputs differ")
    endif()
    if (NOT cpp_${mode}_output MATCHES "cộng" OR NOT vpp_${mode}_output MATCHES "cộng")
        message(FATAL_ERROR "Native AST/IR ${mode}: missing function")
    endif()
    if (mode STREQUAL "ast")
        if (NOT vpp_${mode}_output MATCHES "chương trình" OR
            NOT vpp_${mode}_output MATCHES "hai ngôi" OR
            NOT vpp_${mode}_output MATCHES "hàm tên=\"chính\"")
            message(FATAL_ERROR "Native AST/IR: AST structure incomplete")
        endif()
    else()
        if (NOT vpp_${mode}_output MATCHES "IR \\(V\\+\\+ native\\)" OR
            NOT vpp_${mode}_output MATCHES "toán tử" OR
            NOT vpp_${mode}_output MATCHES "gọi tên")
            message(FATAL_ERROR "Native AST/IR: IR structure incomplete")
        endif()
    endif()
endforeach()

# Dumps are inspections: even a valid call in `chính` must never run.
set(marker "${VPP_OUTPUT_DIR}/dump-must-not-execute.txt")
file(REMOVE "${marker}")
set(no_run_source "${VPP_OUTPUT_DIR}/no-run.vi")
file(WRITE "${no_run_source}"
    "hàm chính() { ghi tệp(\"${marker}\", \"executed\"); };\n")
foreach(mode IN ITEMS ast ir)
    run_dump(no_run_${mode} "${mode}" "${VPP_EXEC}" "--dump-${mode}"
        --compiler=vpp "${no_run_source}")
    if (EXISTS "${marker}")
        message(FATAL_ERROR "Native AST/IR ${mode} executed source")
    endif()
endforeach()

# Class, Unicode, loop, lambda and import metadata: Stage 1 must handle
# these without running any statement or asking the Stage 0 VM for a dump.
set(complex_source "${VPP_OUTPUT_DIR}/complex-fixture.vi")
file(WRITE "${complex_source}"
    "nhập gói.lõi.chuyển kiểu.*;\n"
    "lớp Công Cụ { hàm công khai nhân(x) { trả về x * 2; }; };\n"
    "hàm chính() {\n"
    "  biến dữ liệu = [1, 2, 3];\n"
    "  lặp(i = 0; i < 3; i++) { in dữ liệu[i]; };\n"
    "  biến cộng hai = hàm(x) { trả về x + 2; };\n"
    "  in cộng hai(3);\n"
    "};\n")
file(SHA256 "${complex_source}" original_complex_hash)
run_dump(complex_ast ast "${VPP_EXEC}" --dump-ast --compiler=vpp "${complex_source}")
run_dump(complex_ir ir "${VPP_EXEC}" --dump-ir --compiler=vpp "${complex_source}")
foreach(required IN ITEMS "Công Cụ" "hàm ẩn danh" "lặp" "nhập")
    if (NOT complex_ast_output MATCHES "${required}")
        message(FATAL_ERROR "Native AST/IR: AST missing ${required}")
    endif()
endforeach()
foreach(required IN ITEMS "Công Cụ" "hàm ẩn danh" "kết thúc lặp" "chưa liên kết")
    if (NOT complex_ir_output MATCHES "${required}")
        message(FATAL_ERROR "Native AST/IR: IR missing ${required}")
    endif()
endforeach()

file(WRITE "${VPP_OUTPUT_DIR}/invalid.vi" "hàm f( {\n")
foreach(mode IN ITEMS ast ir)
    foreach(choice IN ITEMS cpp vpp)
        execute_process(
            COMMAND "${CMAKE_COMMAND}" -E env "VPP_HOME=${VPP_ROOT}"
                "VPP_NATIVE_COMPILER=${VPP_NATIVE_COMPILER}"
                "${VPP_EXEC}" "--dump-${mode}" "--compiler=${choice}"
                "${VPP_OUTPUT_DIR}/invalid.vi"
            WORKING_DIRECTORY "${VPP_OUTPUT_DIR}"
            RESULT_VARIABLE status OUTPUT_VARIABLE output ERROR_VARIABLE errors
            TIMEOUT 180)
        if (status STREQUAL "0")
            message(FATAL_ERROR "Native AST/IR ${choice}/${mode} accepted invalid syntax")
        endif()
    endforeach()
endforeach()
file(SHA256 "${source}" final_hash)
file(SHA256 "${complex_source}" final_complex_hash)
if (NOT final_hash STREQUAL original_hash OR
    NOT final_complex_hash STREQUAL original_complex_hash)
    message(FATAL_ERROR "Native AST/IR changed source")
endif()
message(STATUS "Native AST/IR Stage 0, Stage 1, CLI default and direct: PASS")
