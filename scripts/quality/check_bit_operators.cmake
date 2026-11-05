foreach(required IN ITEMS VPP_EXEC VPP_NATIVE_COMPILER VPP_ROOT VPP_OUTPUT_DIR VPP_CC VPP_TARGET)
    if(NOT DEFINED ${required})
        message(FATAL_ERROR "Thiếu ${required} để kiểm tra toán tử bit")
    endif()
endforeach()
file(MAKE_DIRECTORY "${VPP_OUTPUT_DIR}")
set(ENV{VPP_HOME} "${VPP_ROOT}")
set(ENV{VPP_NATIVE_COMPILER} "${VPP_NATIVE_COMPILER}")
set(source "${VPP_ROOT}/src/tests/kiem_tra_toan_tu_bit.vi")
file(READ "${VPP_ROOT}/src/tests/expected/kiem_tra_toan_tu_bit.expected" expected)
if(DEFINED VPP_CPP_ORACLE)
    execute_process(COMMAND "${VPP_CPP_ORACLE}" --bit-parity
        "${VPP_ROOT}/src/tests/fixtures/native_bit_i32.vi"
        RESULT_VARIABLE status OUTPUT_VARIABLE stdout ERROR_VARIABLE stderr TIMEOUT 180)
    if(NOT status STREQUAL "0")
        message(FATAL_ERROR "Mã máy C++ và V++ khác nhau: ${stdout}\n${stderr}")
    endif()
endif()
foreach(compiler IN ITEMS cpp vpp)
    execute_process(COMMAND "${VPP_EXEC}" run "--compiler=${compiler}" "${source}"
        RESULT_VARIABLE status OUTPUT_VARIABLE stdout ERROR_VARIABLE stderr TIMEOUT 60)
    if(NOT status STREQUAL "0" OR NOT stdout STREQUAL expected OR NOT stderr STREQUAL "")
        message(FATAL_ERROR "Sai toán tử bit trên VM/${compiler}: ${status}\n${stdout}\n${stderr}")
    endif()
endforeach()
set(runtimes cpp)
if(DEFINED VPP_AOT_PREPARED_OBJECT)
    list(APPEND runtimes vpp)
endif()
foreach(runtime IN LISTS runtimes)
    set(executable "${VPP_OUTPUT_DIR}/bit-${runtime}")
    execute_process(COMMAND "${CMAKE_COMMAND}" -E env "VPP_AOT_RUNTIME=${runtime}"
        "VPP_AOT_VPP_OBJECT=${VPP_AOT_PREPARED_OBJECT}"
        "${VPP_NATIVE_COMPILER}" build "${source}" -o "${executable}"
        RESULT_VARIABLE status OUTPUT_VARIABLE stdout ERROR_VARIABLE stderr TIMEOUT 180)
    if(NOT status STREQUAL "0")
        message(FATAL_ERROR "Không dựng được AOT/${runtime}: ${stdout}\n${stderr}")
    endif()
    execute_process(COMMAND "${executable}" RESULT_VARIABLE status
        OUTPUT_VARIABLE stdout ERROR_VARIABLE stderr TIMEOUT 30)
    if(NOT status STREQUAL "0" OR NOT stdout STREQUAL expected)
        message(FATAL_ERROR "Sai toán tử bit trên AOT/${runtime}: ${status}\n${stdout}\n${stderr}")
    endif()
    foreach(invalid IN ITEMS "1.5 & 1" "~1.5" "1 << 2.5" "1 | \"a\"")
        file(WRITE "${VPP_OUTPUT_DIR}/invalid.vi" "hàm chính() { in ${invalid}; };\n")
        execute_process(COMMAND "${CMAKE_COMMAND}" -E env "VPP_AOT_RUNTIME=${runtime}"
            "VPP_AOT_VPP_OBJECT=${VPP_AOT_PREPARED_OBJECT}"
            "${VPP_NATIVE_COMPILER}" build "${VPP_OUTPUT_DIR}/invalid.vi" -o "${executable}-error"
            RESULT_VARIABLE status OUTPUT_VARIABLE stdout ERROR_VARIABLE stderr TIMEOUT 180)
        if(NOT status STREQUAL "0")
            message(FATAL_ERROR "Không dựng được kiểm tra kiểu: ${stdout}\n${stderr}")
        endif()
        execute_process(COMMAND "${executable}-error" RESULT_VARIABLE status
            OUTPUT_VARIABLE stdout ERROR_VARIABLE stderr TIMEOUT 30)
        if(status STREQUAL "0" OR NOT stdout STREQUAL "")
            message(FATAL_ERROR "Đã nhận sai kiểu trên AOT/${runtime}: ${invalid}: ${status}\n${stdout}")
        endif()
    endforeach()
endforeach()

# Liên kết với oracle C để kiểm tra 361 cặp, gồm biên i32 và bước dịch âm/lớn.
set(kernels bit_va bit_hoac bit_xor dao_bit dich_trai dich_phai gan_bit uu_tien_bit bit_khung)
set(objects)
foreach(kernel IN LISTS kernels)
    set(object "${VPP_OUTPUT_DIR}/${kernel}.o")
    execute_process(COMMAND "${VPP_NATIVE_COMPILER}" build --target "${VPP_TARGET}"
        --emit-obj "${kernel}" "${VPP_ROOT}/src/tests/fixtures/native_bit_i32.vi" "${object}"
        RESULT_VARIABLE status OUTPUT_VARIABLE stdout ERROR_VARIABLE stderr TIMEOUT 60)
    if(NOT status STREQUAL "0")
        message(FATAL_ERROR "Không phát được mã máy ${kernel}: ${stdout}\n${stderr}")
    endif()
    list(APPEND objects "${object}")
endforeach()
execute_process(COMMAND "${VPP_CC}" "${VPP_ROOT}/scripts/quality/native_bit_smoke.c"
    ${objects} -o "${VPP_OUTPUT_DIR}/bit-native"
    RESULT_VARIABLE status ERROR_VARIABLE stderr TIMEOUT 30)
if(NOT status STREQUAL "0")
    message(FATAL_ERROR "Không liên kết được oracle C: ${stderr}")
endif()
execute_process(COMMAND "${VPP_OUTPUT_DIR}/bit-native" RESULT_VARIABLE status
    OUTPUT_VARIABLE stdout ERROR_VARIABLE stderr TIMEOUT 30)
if(NOT status STREQUAL "0")
    message(FATAL_ERROR "Mã máy sai so với C: ${stdout}\n${stderr}")
endif()
foreach(target IN ITEMS aarch64-apple-darwin aarch64-linux-gnu x86_64-linux-gnu x86_64-windows-msvc)
    foreach(kernel IN LISTS kernels)
        execute_process(COMMAND "${VPP_NATIVE_COMPILER}" build --target "${target}"
            --emit-obj "${kernel}" "${VPP_ROOT}/src/tests/fixtures/native_bit_i32.vi"
            "${VPP_OUTPUT_DIR}/${target}-${kernel}.o"
            RESULT_VARIABLE status OUTPUT_VARIABLE stdout ERROR_VARIABLE stderr TIMEOUT 60)
        if(NOT status STREQUAL "0")
            message(FATAL_ERROR "Không phát được ${target}/${kernel}: ${stdout}\n${stderr}")
        endif()
    endforeach()
endforeach()
message(STATUS "Toán tử bit: VM, AOT, oracle C và object ARM64/x86-64 đều đúng")
