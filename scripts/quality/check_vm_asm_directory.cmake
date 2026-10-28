foreach(required IN ITEMS VPP_VM VPP_STAGE1 VPP_NATIVE_COMPILER VPP_ROOT VPP_OUTPUT_DIR)
    if (NOT DEFINED ${required})
        message(FATAL_ERROR "VM directory test missing ${required}")
    endif()
endforeach()
file(MAKE_DIRECTORY "${VPP_OUTPUT_DIR}")
set(ENV{VPP_HOME} "${VPP_ROOT}")
find_program(VPP_DIR_PYTHON NAMES python3)
set(source "${VPP_OUTPUT_DIR}/directory.vi")
configure_file("${VPP_ROOT}/src/tests/fixtures/vm_asm_directory.vi" "${source}" COPYONLY)
set(image "${VPP_OUTPUT_DIR}/directory.vmb")
execute_process(COMMAND "${VPP_NATIVE_COMPILER}" vm-build "${source}" -o "${image}"
    RESULT_VARIABLE status OUTPUT_VARIABLE output ERROR_VARIABLE errors TIMEOUT 120)
if (NOT status STREQUAL "0")
    message(FATAL_ERROR "Directory fixture compilation failed: ${status}\n${output}\n${errors}")
endif()
set(providers asm)
if (DEFINED VPP_EXEC)
    list(APPEND providers cpp)
endif()
foreach(provider IN LISTS providers)
    file(REMOVE_RECURSE "${VPP_OUTPUT_DIR}/vm-dir-root" "${VPP_OUTPUT_DIR}/vm-dir-external" "${VPP_OUTPUT_DIR}/vm-dir-mode")
    file(REMOVE "${VPP_OUTPUT_DIR}/vm-dir-link")
    file(MAKE_DIRECTORY "${VPP_OUTPUT_DIR}/vm-dir-external")
    file(WRITE "${VPP_OUTPUT_DIR}/vm-dir-external/sentinel" "preserved")
    file(CREATE_LINK "${VPP_OUTPUT_DIR}/vm-dir-external" "${VPP_OUTPUT_DIR}/vm-dir-link" SYMBOLIC)
    if (provider STREQUAL "asm")
        set(command "${CMAKE_COMMAND}" -E env "PATH=" "${VPP_VM}" "${image}")
    else()
        set(command "${VPP_EXEC}" run --compiler=cpp "${source}")
    endif()
    execute_process(COMMAND ${command} WORKING_DIRECTORY "${VPP_OUTPUT_DIR}"
        RESULT_VARIABLE status OUTPUT_VARIABLE output ERROR_VARIABLE errors TIMEOUT 90)
    if (NOT status STREQUAL "0" OR NOT output STREQUAL "VM ASM DIRECTORY/PID OK\n")
        message(FATAL_ERROR "Directory/PID ${provider} failed: ${status}\n${output}\n${errors}")
    endif()
    if (EXISTS "${VPP_OUTPUT_DIR}/vm-dir-root" OR IS_SYMLINK "${VPP_OUTPUT_DIR}/vm-dir-link")
        message(FATAL_ERROR "Directory ${provider} failed to remove its tree/link")
    endif()
    file(READ "${VPP_OUTPUT_DIR}/vm-dir-external/sentinel" sentinel)
    if (NOT sentinel STREQUAL "preserved")
        message(FATAL_ERROR "Directory ${provider} damaged a symlink target")
    endif()
    if (VPP_DIR_PYTHON)
        execute_process(COMMAND "${VPP_DIR_PYTHON}" "${VPP_ROOT}/scripts/quality/check_vm_directory_os.py"
            mode "${VPP_OUTPUT_DIR}/vm-dir-mode"
            RESULT_VARIABLE status ERROR_VARIABLE errors TIMEOUT 10)
        if (NOT status STREQUAL "0")
            message(FATAL_ERROR "Directory ${provider} permissions differ: ${status}\n${errors}")
        endif()
    endif()
    file(REMOVE_RECURSE "${VPP_OUTPUT_DIR}/vm-dir-mode")
endforeach()

if (VPP_DIR_PYTHON)
    file(WRITE "${VPP_OUTPUT_DIR}/pid.vi"
        "hàm chính() { in máy tiến trình mã tiến trình(); in máy tiến trình mã tiến trình cha(); };\n")
    execute_process(COMMAND "${VPP_NATIVE_COMPILER}" vm-build "${VPP_OUTPUT_DIR}/pid.vi"
        -o "${VPP_OUTPUT_DIR}/pid.vmb"
        RESULT_VARIABLE status OUTPUT_VARIABLE output ERROR_VARIABLE errors TIMEOUT 60)
    if (NOT status STREQUAL "0")
        message(FATAL_ERROR "PID oracle fixture build failed: ${status}\n${output}\n${errors}")
    endif()
    execute_process(COMMAND "${VPP_DIR_PYTHON}" "${VPP_ROOT}/scripts/quality/check_vm_directory_os.py"
        pid "${VPP_VM}" "${VPP_OUTPUT_DIR}/pid.vmb" "${VPP_OUTPUT_DIR}"
        RESULT_VARIABLE status ERROR_VARIABLE errors TIMEOUT 20)
    if (NOT status STREQUAL "0")
        message(FATAL_ERROR "PID/PPID differs from OS parent: ${status}\n${errors}")
    endif()
    file(WRITE "${VPP_OUTPUT_DIR}/credentials.vi" [=[hàm chính() {
    in máy tiến trình mã người dùng();
    in máy tiến trình mã người dùng hiệu lực();
    in máy tiến trình mã nhóm();
    in máy tiến trình mã nhóm hiệu lực();
};
]=])
    execute_process(COMMAND "${VPP_NATIVE_COMPILER}" vm-build
        "${VPP_OUTPUT_DIR}/credentials.vi" -o "${VPP_OUTPUT_DIR}/credentials.vmb"
        RESULT_VARIABLE status ERROR_VARIABLE errors TIMEOUT 60)
    if (NOT status STREQUAL "0")
        message(FATAL_ERROR "Credential fixture build failed: ${status}\n${errors}")
    endif()
    execute_process(COMMAND "${VPP_DIR_PYTHON}" "${VPP_ROOT}/scripts/quality/check_vm_directory_os.py"
        credentials "${VPP_VM}" "${VPP_OUTPUT_DIR}/credentials.vmb" "${VPP_OUTPUT_DIR}"
        RESULT_VARIABLE status ERROR_VARIABLE errors TIMEOUT 20)
    if (NOT status STREQUAL "0")
        message(FATAL_ERROR "UID/GID differs from OS: ${status}\n${errors}")
    endif()
endif()

set(invalid_cases
    "máy thư mục tạo(12)" "máy thư mục mở(\"\")"
    "máy thư mục mở(chuỗi từ byte máy ảo([255]))"
    "máy thư mục xóa(chuỗi từ byte máy ảo([97, 0, 98]))"
    "máy thư mục kế(0)" "máy thư mục đóng(1.5)" "máy thư mục kế(\"x\")"
    "t = máy thư mục mở(\".\")[0]\; máy thư mục đóng(t)\; máy thư mục kế(t)"
    "t = máy thư mục mở(\".\")[0]\; máy thư mục đóng(t)\; máy thư mục đóng(t)"
    "t = máy tệp mở(\"vm-dir-external/sentinel\", 0)\; máy thư mục kế(t)")
set(index 0)
foreach(expression IN LISTS invalid_cases)
    math(EXPR index "${index} + 1")
    set(bad_source "${VPP_OUTPUT_DIR}/directory-invalid-${index}.vi")
    set(bad_image "${VPP_OUTPUT_DIR}/directory-invalid-${index}.vmb")
    file(WRITE "${bad_source}" "hàm chính() { ${expression}; in \"unexpected\"; };\n")
    execute_process(COMMAND "${VPP_NATIVE_COMPILER}" vm-build "${bad_source}" -o "${bad_image}"
        RESULT_VARIABLE status OUTPUT_VARIABLE output ERROR_VARIABLE errors TIMEOUT 60)
    if (NOT status STREQUAL "0")
        message(FATAL_ERROR "Directory invalid fixture ${index} build failed: ${status}\n${output}\n${errors}")
    endif()
    foreach(provider IN LISTS providers)
        if (provider STREQUAL "asm")
            set(command "${CMAKE_COMMAND}" -E env "PATH=" "${VPP_VM}" "${bad_image}")
        else()
            set(command "${VPP_EXEC}" run --compiler=cpp "${bad_source}")
        endif()
        execute_process(COMMAND ${command} WORKING_DIRECTORY "${VPP_OUTPUT_DIR}"
            RESULT_VARIABLE status OUTPUT_VARIABLE output ERROR_VARIABLE errors TIMEOUT 30)
        if (status STREQUAL "0" OR NOT output STREQUAL "" OR
            (provider STREQUAL "asm" AND NOT errors MATCHES "error=2"))
            message(FATAL_ERROR "Directory ${provider} accepted ${expression}: ${status}\n${output}\n${errors}")
        endif()
    endforeach()
endforeach()

set(native "${VPP_OUTPUT_DIR}/native-directory-decode")
set(native_source "${VPP_OUTPUT_DIR}/native-directory-decode.vi")
file(WRITE "${native_source}" "")
foreach(part IN ITEMS "bộ nhớ" "giá trị" "máy hệ thống" "thư mục máy" "tiến trình máy" "ổ cắm máy"
        "phân giải máy" "host ABI" "nạp chương trình" "runtime" "thực thi")
    file(READ "${VPP_ROOT}/gói/máy ảo/${part}.vi" contents)
    file(APPEND "${native_source}" "${contents}\n")
endforeach()
file(READ "${VPP_ROOT}/src/tests/fixtures/native_directory_decode.vi" contents)
file(APPEND "${native_source}" "${contents}\n")
execute_process(COMMAND "${VPP_STAGE1}" build
    "${native_source}" -o "${native}"
    RESULT_VARIABLE status OUTPUT_VARIABLE output ERROR_VARIABLE errors TIMEOUT 180)
if (NOT status STREQUAL "0")
    message(FATAL_ERROR "Directory decoder build failed: ${status}\n${output}\n${errors}")
endif()
execute_process(COMMAND "${CMAKE_COMMAND}" -E env "PATH=" "${native}"
    WORKING_DIRECTORY "${VPP_OUTPUT_DIR}"
    RESULT_VARIABLE status OUTPUT_VARIABLE output ERROR_VARIABLE errors TIMEOUT 30)
if (NOT status STREQUAL "0")
    message(FATAL_ERROR "Directory decoder/ownership/cleanup failed: ${status}\n${output}\n${errors}")
endif()
message(STATUS "V++/ASM directories, PID/PPID, UTF-8, tokens, cleanup and Stage 0 parity PASS")
