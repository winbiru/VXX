foreach(required IN ITEMS VPP_NATIVE_COMPILER VPP_ROOT VPP_OUTPUT_DIR)
    if(NOT DEFINED ${required})
        message(FATAL_ERROR "Thiếu ${required} để kiểm tra biểu thức i32 AOT")
    endif()
endforeach()
file(MAKE_DIRECTORY "${VPP_OUTPUT_DIR}")
file(READ "${VPP_ROOT}/src/tests/expected/native_i32_expression.expected" expected)
set(runtimes cpp)
if(DEFINED VPP_AOT_PREPARED_OBJECT)
    list(APPEND runtimes vpp)
endif()
foreach(runtime IN LISTS runtimes)
    set(output "${VPP_OUTPUT_DIR}/i32-expression-${runtime}")
    execute_process(COMMAND "${CMAKE_COMMAND}" -E env
        "VPP_HOME=${VPP_ROOT}" "VPP_AOT_RUNTIME=${runtime}" "VPP_AOT_KEEP_ASM=1"
        "VPP_AOT_VPP_OBJECT=${VPP_AOT_PREPARED_OBJECT}"
        "${VPP_NATIVE_COMPILER}" build
        "${VPP_ROOT}/src/tests/fixtures/native_i32_expression.vi" -o "${output}"
        RESULT_VARIABLE status OUTPUT_VARIABLE stdout ERROR_VARIABLE stderr TIMEOUT 180)
    if(NOT status STREQUAL "0")
        message(FATAL_ERROR "Không dựng được biểu thức i32/${runtime}: ${stdout}\n${stderr}")
    endif()
    file(READ "${output}.S" assembly)
    if(NOT assembly MATCHES "vpp_native_dynamic_is_i32" OR
       NOT assembly MATCHES "vpp_native_dynamic_read_i32")
        message(FATAL_ERROR "Bộ hạ V++ chưa phát đường kiểm tra và nạp i32")
    endif()
    # Xác nhận có cả mã máy của hàm tĩnh và lệnh gọi ABI trực tiếp.
    foreach(id IN ITEMS 0 1)
        if(NOT assembly MATCHES "vpp_aot_i32_${id}:")
            message(FATAL_ERROR "Chưa nhận diện hàm i32 tĩnh ${id}/${runtime}")
        endif()
        if(NOT assembly MATCHES "(bl|call)[ \t]+_?vpp_aot_i32_${id}([ \t\n]|$)")
            message(FATAL_ERROR "Chưa gọi hàm i32 ${id} qua ABI máy/${runtime}")
        endif()
    endforeach()
    execute_process(COMMAND "${output}" RESULT_VARIABLE status
        OUTPUT_VARIABLE stdout ERROR_VARIABLE stderr TIMEOUT 30)
    if(NOT status STREQUAL "0" OR NOT stdout STREQUAL expected)
        message(FATAL_ERROR "Sai kết quả biểu thức i32/${runtime}: ${status}\n${stdout}\n${stderr}")
    endif()
    set(backend_calls "${VPP_OUTPUT_DIR}/i32-backend-calls-${runtime}")
    execute_process(COMMAND "${CMAKE_COMMAND}" -E env
        "VPP_HOME=${VPP_ROOT}" "VPP_AOT_RUNTIME=${runtime}" "VPP_AOT_KEEP_ASM=1"
        "VPP_AOT_VPP_OBJECT=${VPP_AOT_PREPARED_OBJECT}"
        "${VPP_NATIVE_COMPILER}" build
        "${VPP_ROOT}/src/tests/fixtures/native_i32_backend_calls.vi" -o "${backend_calls}"
        RESULT_VARIABLE status OUTPUT_VARIABLE stdout ERROR_VARIABLE stderr TIMEOUT 180)
    if(NOT status STREQUAL "0")
        message(FATAL_ERROR "Không dựng được backend i32 dùng chung/${runtime}: ${stdout}\n${stderr}")
    endif()
    file(READ "${backend_calls}.S" backend_asm)
    foreach(id IN ITEMS 1 2 4)
        if(NOT backend_asm MATCHES "vpp_aot_i32_${id}:")
            message(FATAL_ERROR "Thiếu thân native có lời gọi trong vòng/nhánh ${id}/${runtime}")
        endif()
    endforeach()
    file(READ "${VPP_ROOT}/src/tests/expected/native_i32_backend_calls.expected" backend_expected)
    execute_process(COMMAND "${backend_calls}" RESULT_VARIABLE status
        OUTPUT_VARIABLE stdout ERROR_VARIABLE stderr TIMEOUT 30)
    if(NOT status STREQUAL "0" OR NOT stdout STREQUAL backend_expected)
        message(FATAL_ERROR "Sai backend i32 hoặc nhánh động/${runtime}: ${status}\n${stdout}\n${stderr}")
    endif()
    execute_process(COMMAND "${CMAKE_COMMAND}" -E env
        "VPP_HOME=${VPP_ROOT}" "VPP_AOT_RUNTIME=${runtime}"
        "VPP_AOT_VPP_OBJECT=${VPP_AOT_PREPARED_OBJECT}"
        "${VPP_NATIVE_COMPILER}" build
        "${VPP_ROOT}/src/tests/fixtures/native_i32_expression_error.vi" -o "${output}-error"
        RESULT_VARIABLE status OUTPUT_VARIABLE stdout ERROR_VARIABLE stderr TIMEOUT 180)
    if(NOT status STREQUAL "0")
        message(FATAL_ERROR "Không dựng được kiểm tra lỗi chia: ${stdout}\n${stderr}")
    endif()
    execute_process(COMMAND "${output}-error" RESULT_VARIABLE status
        OUTPUT_VARIABLE stdout ERROR_VARIABLE stderr TIMEOUT 30)
    if(NOT status STREQUAL "1" OR NOT stdout STREQUAL "")
        message(FATAL_ERROR "Đã bỏ qua lỗi chia/${runtime}: ${status}\n${stdout}\n${stderr}")
    endif()
    set(calls_output "${VPP_OUTPUT_DIR}/i32-direct-calls-${runtime}")
    execute_process(COMMAND "${CMAKE_COMMAND}" -E env
        "VPP_HOME=${VPP_ROOT}" "VPP_AOT_RUNTIME=${runtime}" "VPP_AOT_KEEP_ASM=1"
        "VPP_AOT_VPP_OBJECT=${VPP_AOT_PREPARED_OBJECT}"
        "${VPP_NATIVE_COMPILER}" build
        "${VPP_ROOT}/src/tests/fixtures/native_i32_direct_calls.vi" -o "${calls_output}"
        RESULT_VARIABLE status OUTPUT_VARIABLE stdout ERROR_VARIABLE stderr TIMEOUT 180)
    if(NOT status STREQUAL "0")
        message(FATAL_ERROR "Không dựng được lời gọi i32 tĩnh/${runtime}: ${stdout}\n${stderr}")
    endif()
    file(READ "${calls_output}.S" calls_asm)
    foreach(id IN ITEMS 0 1 2 3)
        if(NOT calls_asm MATCHES "vpp_aot_i32_${id}:")
            message(FATAL_ERROR "Thiếu hàm tĩnh có ${id} đối số/${runtime}")
        endif()
        if(NOT calls_asm MATCHES "(bl|call)[ \t]+_?vpp_aot_i32_${id}([ \t\n]|$)")
            message(FATAL_ERROR "Thiếu lời gọi trực tiếp i32 có ${id} đối số/${runtime}")
        endif()
    endforeach()
    foreach(id IN ITEMS 4 5)
        if(calls_asm MATCHES "vpp_aot_i32_${id}:")
            message(FATAL_ERROR "Chuyên biệt hóa nhầm hàm 5 đối số hoặc tham số mặc định/${runtime}")
        endif()
    endforeach()
    file(READ "${VPP_ROOT}/src/tests/expected/native_i32_direct_calls.expected" calls_expected)
    execute_process(COMMAND "${calls_output}" RESULT_VARIABLE status
        OUTPUT_VARIABLE stdout ERROR_VARIABLE stderr TIMEOUT 30)
    if(NOT status STREQUAL "0" OR NOT stdout STREQUAL calls_expected)
        message(FATAL_ERROR "Sai lời gọi trực tiếp hoặc nhánh chậm/${runtime}: ${status}\n${stdout}\n${stderr}")
    endif()
    if(runtime STREQUAL "cpp")
        # Có 9 lời gọi trong chính(): 5 lời gọi i32 nhanh, 4 lời gọi chậm
        # (hàm 5 tham số, mặc định và 2 số thực). Thêm một lần vào chính().
        execute_process(COMMAND "${CMAKE_COMMAND}" -E env VPP_AOT_CPU_PROFILE=1
            "${calls_output}" RESULT_VARIABLE status
            OUTPUT_VARIABLE stdout ERROR_VARIABLE stderr TIMEOUT 30)
        if(NOT status STREQUAL "0" OR NOT stdout STREQUAL calls_expected OR
           NOT stderr MATCHES "invoke.total calls=5 ")
            message(FATAL_ERROR "Lời gọi i32 còn tạo khung động: ${status}\n${stdout}\n${stderr}")
        endif()
    endif()

    # Các đối số có thể là cả cây hậu tố i32, không chỉ hằng số/ô biến.
    # Lặp lại cùng một ô và đổi kiểu sau đó đều phải trả về đúng kết quả.
    set(expr_calls_output "${VPP_OUTPUT_DIR}/i32-expression-calls-${runtime}")
    execute_process(COMMAND "${CMAKE_COMMAND}" -E env
        "VPP_HOME=${VPP_ROOT}" "VPP_AOT_RUNTIME=${runtime}" "VPP_AOT_KEEP_ASM=1"
        "VPP_AOT_VPP_OBJECT=${VPP_AOT_PREPARED_OBJECT}"
        "${VPP_NATIVE_COMPILER}" build
        "${VPP_ROOT}/src/tests/fixtures/native_i32_expression_calls.vi"
        -o "${expr_calls_output}"
        RESULT_VARIABLE status OUTPUT_VARIABLE stdout ERROR_VARIABLE stderr TIMEOUT 180)
    if(NOT status STREQUAL "0")
        message(FATAL_ERROR "Không dựng được lời gọi có biểu thức i32/${runtime}: ${stdout}\n${stderr}")
    endif()
    file(READ "${expr_calls_output}.S" expr_calls_asm)
    foreach(id IN ITEMS 0 1)
        if(NOT expr_calls_asm MATCHES "vpp_aot_i32_${id}:" OR
           NOT expr_calls_asm MATCHES "(bl|call)[ \t]+_?vpp_aot_i32_${id}([ \t\n]|$)")
            message(FATAL_ERROR "Không hạ biểu thức i32 vào thanh ghi của hàm ${id}/${runtime}")
        endif()
    endforeach()
    file(READ "${VPP_ROOT}/src/tests/expected/native_i32_expression_calls.expected" expr_calls_expected)
    execute_process(COMMAND "${expr_calls_output}" RESULT_VARIABLE status
        OUTPUT_VARIABLE stdout ERROR_VARIABLE stderr TIMEOUT 30)
    if(NOT status STREQUAL "0" OR NOT stdout STREQUAL expr_calls_expected)
        message(FATAL_ERROR "Sai ngữ nghĩa biểu thức đối số i32/${runtime}: ${status}\n${stdout}\n${stderr}")
    endif()
    if(runtime STREQUAL "cpp")
        # Ba lời gọi sử dụng i32 máy; ba lời gọi phải chuyển về runtime
        # (hai số thực và một nhánh biểu thức đổi kiểu).
        execute_process(COMMAND "${CMAKE_COMMAND}" -E env VPP_AOT_CPU_PROFILE=1
            "${expr_calls_output}" RESULT_VARIABLE status
            OUTPUT_VARIABLE stdout ERROR_VARIABLE stderr TIMEOUT 30)
        if(NOT status STREQUAL "0" OR NOT stdout STREQUAL expr_calls_expected OR
           NOT stderr MATCHES "invoke.total calls=3 ")
            message(FATAL_ERROR "Biểu thức i32 chưa đi qua lời gọi nhanh: ${status}\n${stdout}\n${stderr}")
        endif()
    endif()

    # Lồng lời gọi i32 thuần trực tiếp trong các đối số, gồm hàm không
    # tham số. Khi giá trị chuyển sang số thực phải thực thi đúng nhánh chậm.
    set(nested_output "${VPP_OUTPUT_DIR}/i32-nested-calls-${runtime}")
    execute_process(COMMAND "${CMAKE_COMMAND}" -E env
        "VPP_HOME=${VPP_ROOT}" "VPP_AOT_RUNTIME=${runtime}" "VPP_AOT_KEEP_ASM=1"
        "VPP_AOT_VPP_OBJECT=${VPP_AOT_PREPARED_OBJECT}"
        "${VPP_NATIVE_COMPILER}" build
        "${VPP_ROOT}/src/tests/fixtures/native_i32_nested_calls.vi" -o "${nested_output}"
        RESULT_VARIABLE status OUTPUT_VARIABLE stdout ERROR_VARIABLE stderr TIMEOUT 180)
    if(NOT status STREQUAL "0")
        message(FATAL_ERROR "Không dựng được lời gọi i32 lồng nhau/${runtime}: ${stdout}\n${stderr}")
    endif()
    file(READ "${nested_output}.S" nested_asm)
    foreach(id IN ITEMS 0 1 2)
        if(NOT nested_asm MATCHES "vpp_aot_i32_${id}:" OR
           NOT nested_asm MATCHES "(bl|call)[ \t]+_?vpp_aot_i32_${id}([ \t\n]|$)")
            message(FATAL_ERROR "Không hạ lời gọi i32 lồng nhau ${id}/${runtime}")
        endif()
    endforeach()
    # Cả năm lời gọi trộn phải có nhánh ABI trực tiếp, kể cả lời gọi
    # lồng nhiều tầng và lời gọi có đường dự phòng cho số thực.
    string(REGEX MATCHALL "(bl|call)[ \t]+_?vpp_aot_i32_2([ \t\n]|$)"
        nested_outer_calls "${nested_asm}")
    list(LENGTH nested_outer_calls nested_outer_count)
    if(nested_outer_count LESS 5)
        message(FATAL_ERROR "Chỉ hạ ${nested_outer_count}/5 lời gọi i32 ngoài cùng/${runtime}")
    endif()
    file(READ "${VPP_ROOT}/src/tests/expected/native_i32_nested_calls.expected" nested_expected)
    execute_process(COMMAND "${nested_output}" RESULT_VARIABLE status
        OUTPUT_VARIABLE stdout ERROR_VARIABLE stderr TIMEOUT 30)
    if(NOT status STREQUAL "0" OR NOT stdout STREQUAL nested_expected)
        message(FATAL_ERROR "Sai kết quả lời gọi i32 lồng nhau/${runtime}: ${status}\n${stdout}\n${stderr}")
    endif()
    if(runtime STREQUAL "cpp")
        # Một khung của chính() và ba lời gọi động từ nhánh số thực;
        # các lời gọi i32 còn lại không cần tạo khung runtime.
        execute_process(COMMAND "${CMAKE_COMMAND}" -E env VPP_AOT_CPU_PROFILE=1
            "${nested_output}" RESULT_VARIABLE status
            OUTPUT_VARIABLE stdout ERROR_VARIABLE stderr TIMEOUT 30)
        if(NOT status STREQUAL "0" OR NOT stdout STREQUAL nested_expected OR
           NOT stderr MATCHES "invoke.total calls=4 " OR
           NOT stderr MATCHES "abi.call calls=4 ")
            message(FATAL_ERROR "Lời gọi i32 lồng nhau còn tạo khung động: ${status}\n${stdout}\n${stderr}")
        endif()
    endif()

    # Kiểm tra suy luận lan truyền qua chuỗi hàm i32: hàm gọi hàm thuần
    # cũng phải dùng BL theo ABI, trong khi hàm có hiệu ứng giữ runtime.
    set(chain_output "${VPP_OUTPUT_DIR}/i32-call-chain-${runtime}")
    execute_process(COMMAND "${CMAKE_COMMAND}" -E env
        "VPP_HOME=${VPP_ROOT}" "VPP_AOT_RUNTIME=${runtime}" "VPP_AOT_KEEP_ASM=1"
        "VPP_AOT_VPP_OBJECT=${VPP_AOT_PREPARED_OBJECT}"
        "${VPP_NATIVE_COMPILER}" build
        "${VPP_ROOT}/src/tests/fixtures/native_i32_call_chain.vi" -o "${chain_output}"
        RESULT_VARIABLE status OUTPUT_VARIABLE stdout ERROR_VARIABLE stderr TIMEOUT 180)
    if(NOT status STREQUAL "0")
        message(FATAL_ERROR "Không dựng được chuỗi hàm i32/${runtime}: ${stdout}\n${stderr}")
    endif()
    file(READ "${chain_output}.S" chain_asm)
    foreach(id IN ITEMS 0 1 2 3 4)
        if(NOT chain_asm MATCHES "vpp_aot_i32_${id}:")
            message(FATAL_ERROR "Không chứng minh được hàm i32 số ${id}/${runtime}")
        endif()
        if(NOT chain_asm MATCHES "(bl|call)[ \t]+_?vpp_aot_i32_${id}([ \t\n]|$)")
            message(FATAL_ERROR "Thiếu lời gọi ABI i32 số ${id}/${runtime}")
        endif()
    endforeach()
    if(chain_asm MATCHES "vpp_aot_i32_5:")
        message(FATAL_ERROR "Đã đưa hàm có hiệu ứng phụ vào đường thuần i32/${runtime}")
    endif()
    file(READ "${VPP_ROOT}/src/tests/expected/native_i32_call_chain.expected" chain_expected)
    execute_process(COMMAND "${chain_output}" RESULT_VARIABLE status
        OUTPUT_VARIABLE stdout ERROR_VARIABLE stderr TIMEOUT 30)
    if(NOT status STREQUAL "0" OR NOT stdout STREQUAL chain_expected)
        message(FATAL_ERROR "Chuỗi gọi i32 hoặc nhánh động sai/${runtime}: ${status}\n${stdout}\n${stderr}")
    endif()
    foreach(test IN ITEMS native_aot_slots native_aot_index)
        set(output "${VPP_OUTPUT_DIR}/${test}-${runtime}")
        execute_process(COMMAND "${CMAKE_COMMAND}" -E env
            "VPP_HOME=${VPP_ROOT}" "VPP_AOT_RUNTIME=${runtime}" "VPP_AOT_KEEP_ASM=1"
            "VPP_AOT_VPP_OBJECT=${VPP_AOT_PREPARED_OBJECT}"
            "${VPP_NATIVE_COMPILER}" build "${VPP_ROOT}/src/tests/fixtures/${test}.vi" -o "${output}"
            RESULT_VARIABLE status OUTPUT_VARIABLE stdout ERROR_VARIABLE stderr TIMEOUT 180)
        if(NOT status STREQUAL "0")
            message(FATAL_ERROR "Không dựng được ${test}/${runtime}: ${stdout}\n${stderr}")
        endif()
        file(READ "${VPP_ROOT}/src/tests/expected/${test}.expected" fused_expected)
        execute_process(COMMAND "${CMAKE_COMMAND}" -E env VPP_VM_PROFILE=1 "${output}"
            RESULT_VARIABLE status OUTPUT_VARIABLE stdout ERROR_VARIABLE stderr TIMEOUT 60)
        if(NOT status STREQUAL "0" OR NOT stdout STREQUAL fused_expected)
            message(FATAL_ERROR "Sai ${test}/${runtime}: ${status}\n${stdout}\n${stderr}")
        endif()
        if(runtime STREQUAL "vpp" AND test STREQUAL "native_aot_slots" AND
           NOT stderr MATCHES "collections=[1-9][0-9]* ")
            message(FATAL_ERROR "Chưa thực sự thu gom khi đối số các khung cha còn sống")
        endif()
        file(READ "${output}.S" assembly)
        set(symbols copy_slot)
        if(test STREQUAL "native_aot_index")
            set(symbols index_equal index_update index_mod_add_if_equal
                list_append_i32_loop list_mod_sum_loop range_mod_sum_i32_loop)
        endif()
        foreach(symbol IN LISTS symbols)
            if(NOT assembly MATCHES "(bl|call)[ \t]+_?vpp_native_dynamic_${symbol}([ \t\n]|$)")
                message(FATAL_ERROR "Chưa phát lời gọi ${symbol}/${runtime}")
            endif()
        endforeach()
        if(test STREQUAL "native_aot_index")
            # `tạo lọc tổng` là hàm số 7 trong fixture. Hàm phải được hạ
            # thành i32 native và bộ chọn nhanh phải gọi thẳng symbol này.
            if(NOT assembly MATCHES "vpp_aot_i32_7:" OR
               NOT assembly MATCHES "(bl|call)[ \\t]+_*vpp_aot_i32_7")
                message(FATAL_ERROR "Chưa hạ dãy/lọc/tổng thành hàm i32 native/${runtime}")
            endif()
        endif()
    endforeach()

    # Private method của chính lớp được phép bỏ lookup khi toàn bộ cây lớp
    # chứng minh không có descendant che cùng tên. Bao phủ 0-2 đối số, hằng,
    # ô biến và chuỗi để giữ parity giữa backend C++ và compiler V++ tự chủ.
    set(private_output "${VPP_OUTPUT_DIR}/native-aot-private-method-${runtime}")
    execute_process(COMMAND "${CMAKE_COMMAND}" -E env
        "VPP_HOME=${VPP_ROOT}" "VPP_AOT_RUNTIME=${runtime}" "VPP_AOT_KEEP_ASM=1"
        "VPP_AOT_VPP_OBJECT=${VPP_AOT_PREPARED_OBJECT}"
        "${VPP_NATIVE_COMPILER}" build
        "${VPP_ROOT}/src/tests/fixtures/native_aot_private_method.vi" -o "${private_output}"
        RESULT_VARIABLE status OUTPUT_VARIABLE stdout ERROR_VARIABLE stderr TIMEOUT 180)
    if(NOT status STREQUAL "0")
        message(FATAL_ERROR "Không dựng được private method trực tiếp/${runtime}: ${stdout}\n${stderr}")
    endif()
    file(READ "${private_output}.S" private_asm)
    string(REGEX MATCHALL "(bl|call)[ \t]+_?vpp_native_dynamic_method_direct_small([ \t\n]|$)"
        private_calls "${private_asm}")
    list(LENGTH private_calls private_call_count)
    if(private_call_count LESS 5)
        message(FATAL_ERROR "Chỉ hạ ${private_call_count}/5 private method trực tiếp/${runtime}")
    endif()
    file(READ "${VPP_ROOT}/src/tests/expected/native_aot_private_method.expected" private_expected)
    execute_process(COMMAND "${private_output}" RESULT_VARIABLE status
        OUTPUT_VARIABLE stdout ERROR_VARIABLE stderr TIMEOUT 30)
    if(NOT status STREQUAL "0" OR NOT stdout STREQUAL private_expected)
        message(FATAL_ERROR "Sai private method trực tiếp/${runtime}: ${status}\n${stdout}\n${stderr}")
    endif()

    # Callee local đứng ngay trước OP_GOI_GIAN_TIEP phải được đọc trực tiếp
    # từ slot. Bao phủ function id, lookup bằng chuỗi và closure có capture.
    set(indirect_slot_output "${VPP_OUTPUT_DIR}/native-aot-indirect-slot-${runtime}")
    execute_process(COMMAND "${CMAKE_COMMAND}" -E env
        "VPP_HOME=${VPP_ROOT}" "VPP_AOT_RUNTIME=${runtime}" "VPP_AOT_KEEP_ASM=1"
        "VPP_AOT_VPP_OBJECT=${VPP_AOT_PREPARED_OBJECT}"
        "${VPP_NATIVE_COMPILER}" build
        "${VPP_ROOT}/src/tests/fixtures/native_aot_indirect_slot.vi" -o "${indirect_slot_output}"
        RESULT_VARIABLE status OUTPUT_VARIABLE stdout ERROR_VARIABLE stderr TIMEOUT 180)
    if(NOT status STREQUAL "0")
        message(FATAL_ERROR "Không dựng được indirect slot/${runtime}: ${stdout}\n${stderr}")
    endif()
    file(READ "${indirect_slot_output}.S" indirect_slot_asm)
    string(REGEX MATCHALL "(bl|call)[ \t]+_?vpp_native_dynamic_indirect_slot([ \t\n]|$)"
        indirect_slot_calls "${indirect_slot_asm}")
    list(LENGTH indirect_slot_calls indirect_slot_call_count)
    if(indirect_slot_call_count LESS 3)
        message(FATAL_ERROR "Chỉ hạ ${indirect_slot_call_count}/3 lời gọi qua slot/${runtime}")
    endif()
    if(NOT indirect_slot_asm MATCHES "vpp_aot_fast_s1:" OR
       NOT indirect_slot_asm MATCHES "vpp_aot_fast_s2:")
        message(FATAL_ERROR "Thiếu selector slot i32 cho lời gọi gián tiếp/${runtime}")
    endif()
    file(READ "${VPP_ROOT}/src/tests/expected/native_aot_indirect_slot.expected" indirect_slot_expected)
    execute_process(COMMAND "${indirect_slot_output}" RESULT_VARIABLE status
        OUTPUT_VARIABLE stdout ERROR_VARIABLE stderr TIMEOUT 30)
    if(NOT status STREQUAL "0" OR NOT stdout STREQUAL indirect_slot_expected)
        message(FATAL_ERROR "Sai indirect slot/${runtime}: ${status}\n${stdout}\n${stderr}")
    endif()

    set(indirect_slot_error_output "${VPP_OUTPUT_DIR}/native-aot-indirect-slot-error-${runtime}")
    execute_process(COMMAND "${CMAKE_COMMAND}" -E env
        "VPP_HOME=${VPP_ROOT}" "VPP_AOT_RUNTIME=${runtime}"
        "VPP_AOT_VPP_OBJECT=${VPP_AOT_PREPARED_OBJECT}"
        "${VPP_NATIVE_COMPILER}" build
        "${VPP_ROOT}/src/tests/fixtures/native_aot_indirect_slot_error.vi"
        -o "${indirect_slot_error_output}"
        RESULT_VARIABLE status OUTPUT_VARIABLE stdout ERROR_VARIABLE stderr TIMEOUT 180)
    if(NOT status STREQUAL "0")
        message(FATAL_ERROR "Không dựng được lỗi indirect slot/${runtime}: ${stdout}\n${stderr}")
    endif()
    execute_process(COMMAND "${indirect_slot_error_output}" RESULT_VARIABLE status
        OUTPUT_VARIABLE stdout ERROR_VARIABLE stderr TIMEOUT 30)
    if(NOT status STREQUAL "1" OR NOT stdout STREQUAL "" OR
       NOT stderr MATCHES "not callable|không.*gọi|error=13")
        message(FATAL_ERROR "Sai lỗi indirect slot/${runtime}: ${status}\n${stdout}\n${stderr}")
    endif()

    # Nếu lớp con khai báo cùng tên, lookup động của VM có thể chọn method đó
    # trước rồi từ chối quyền truy cập. Direct-call lớp cha trong trường hợp này
    # sẽ đổi ngữ nghĩa nên AOT phải giữ đường động.
    set(shadow_output "${VPP_OUTPUT_DIR}/native-aot-private-shadow-${runtime}")
    execute_process(COMMAND "${CMAKE_COMMAND}" -E env
        "VPP_HOME=${VPP_ROOT}" "VPP_AOT_RUNTIME=${runtime}" "VPP_AOT_KEEP_ASM=1"
        "VPP_AOT_VPP_OBJECT=${VPP_AOT_PREPARED_OBJECT}"
        "${VPP_NATIVE_COMPILER}" build
        "${VPP_ROOT}/src/tests/fixtures/native_aot_private_shadow.vi" -o "${shadow_output}"
        RESULT_VARIABLE status OUTPUT_VARIABLE stdout ERROR_VARIABLE stderr TIMEOUT 180)
    if(NOT status STREQUAL "0")
        message(FATAL_ERROR "Không dựng được kiểm tra private method bị che/${runtime}: ${stdout}\n${stderr}")
    endif()
    file(READ "${shadow_output}.S" shadow_asm)
    if(shadow_asm MATCHES "(bl|call)[ \t]+_?vpp_native_dynamic_method_direct_small([ \t\n]|$)")
        message(FATAL_ERROR "Đã direct-call private method bị descendant che/${runtime}")
    endif()
    execute_process(COMMAND "${shadow_output}" RESULT_VARIABLE status
        OUTPUT_VARIABLE stdout ERROR_VARIABLE stderr TIMEOUT 30)
    if(NOT status STREQUAL "1" OR NOT stdout STREQUAL "" OR
       NOT stderr MATCHES "method access denied: kín|error=19")
        message(FATAL_ERROR "Sai ngữ nghĩa private method bị che/${runtime}: ${status}\n${stdout}\n${stderr}")
    endif()

    set(output "${VPP_OUTPUT_DIR}/native_aot_index_error-${runtime}")
    execute_process(COMMAND "${CMAKE_COMMAND}" -E env
        "VPP_HOME=${VPP_ROOT}" "VPP_AOT_RUNTIME=${runtime}"
        "VPP_AOT_VPP_OBJECT=${VPP_AOT_PREPARED_OBJECT}"
        "${VPP_NATIVE_COMPILER}" build "${VPP_ROOT}/src/tests/fixtures/native_aot_index_error.vi" -o "${output}"
        RESULT_VARIABLE status OUTPUT_VARIABLE stdout ERROR_VARIABLE stderr TIMEOUT 180)
    if(NOT status STREQUAL "0")
        message(FATAL_ERROR "Không dựng được kiểm tra lỗi chỉ số/${runtime}: ${stdout}\n${stderr}")
    endif()
    foreach(case IN ITEMS "âm" "cuối" "kiểu" "danh sách" "phép toán" "truy cập" "tham số")
        execute_process(COMMAND "${CMAKE_COMMAND}" -E env "VPP_AOT_TEST_ERROR=${case}" "${output}"
            RESULT_VARIABLE status OUTPUT_VARIABLE stdout ERROR_VARIABLE stderr TIMEOUT 30)
        if(NOT status STREQUAL "1" OR NOT stdout STREQUAL "")
            message(FATAL_ERROR "Đã bỏ qua lỗi ${case}/${runtime}: ${status}\n${stdout}\n${stderr}")
        endif()
    endforeach()
    # Chuỗi thô giữ byte UTF-8 malformed như một đơn vị dài 1, giống
    # utf8CodePointAt/utf8CodePointCount của bootstrap. JSON có bộ kiểm tra
    # UTF-8 nghiêm ngặt riêng nên thay đổi này không làm JSON nhận dữ liệu lỗi.
    execute_process(COMMAND "${CMAKE_COMMAND}" -E env VPP_AOT_TEST_ERROR=utf8 "${output}"
        RESULT_VARIABLE status OUTPUT_VARIABLE stdout ERROR_VARIABLE stderr TIMEOUT 30)
    if(NOT status STREQUAL "0" OR NOT stdout STREQUAL "đã bỏ qua lỗi\n" OR stderr MATCHES "error=3")
        message(FATAL_ERROR "Sai quy tắc giữ byte UTF-8 malformed/${runtime}: ${status}\n${stdout}\n${stderr}")
    endif()
endforeach()

# Kiểm tra đường nhanh thực sự chạy, thay vì chỉ tìm symbol trong mã dự phòng.
file(WRITE "${VPP_OUTPUT_DIR}/i32-only.vi"
    "hàm chính() { n = 17; in (n + 17) * 31 + (n * 7 + 3) - (n + 5) * 11; };\n")
execute_process(COMMAND "${CMAKE_COMMAND}" -E env "VPP_HOME=${VPP_ROOT}"
    VPP_AOT_RUNTIME=cpp "${VPP_NATIVE_COMPILER}" build
    "${VPP_OUTPUT_DIR}/i32-only.vi" -o "${VPP_OUTPUT_DIR}/i32-only"
    RESULT_VARIABLE status OUTPUT_VARIABLE stdout ERROR_VARIABLE stderr TIMEOUT 180)
if(NOT status STREQUAL "0")
    message(FATAL_ERROR "Không dựng được phép đo đường nhanh: ${stdout}\n${stderr}")
endif()
execute_process(COMMAND "${CMAKE_COMMAND}" -E env VPP_AOT_CPU_PROFILE=1
    "${VPP_OUTPUT_DIR}/i32-only" RESULT_VARIABLE status
    OUTPUT_VARIABLE stdout ERROR_VARIABLE stderr TIMEOUT 30)
if(NOT status STREQUAL "0" OR NOT stdout STREQUAL "934\n" OR
   NOT stderr MATCHES "abi.read_i32 calls=1 " OR stderr MATCHES "abi.binary calls=[1-9]")
    message(FATAL_ERROR "Biểu thức i32 còn chạy phép toán động: ${status}\n${stdout}\n${stderr}")
endif()
message(STATUS "Biểu thức i32, đổi kiểu, tràn số, bao đóng và lỗi chia: PASS")
