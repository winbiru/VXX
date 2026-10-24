# Stage 0 produces a standalone V++ compiler; its own frontend and object
# writer compile subsequent leaf programs, without delegating to the C++ VM.
if (NOT DEFINED VPP_EXEC OR NOT DEFINED VPP_ROOT OR NOT DEFINED VPP_TARGET OR
    NOT DEFINED VPP_OUTPUT_DIR)
    message(FATAL_ERROR "missing native compiler-driver test arguments")
endif()
file(MAKE_DIRECTORY "${VPP_OUTPUT_DIR}")
set(source "${VPP_OUTPUT_DIR}/vpp-native-source.vi")
set(module_entry "${VPP_ROOT}/gói/biên dịch/bản địa/bộ biên dịch.vi")
set(compiler "${VPP_OUTPUT_DIR}/vpp-native")
set(stage0_linux FALSE)
if (VPP_TARGET MATCHES "^x86_64-.*linux")
    set(stage0_linux TRUE)
endif()
file(WRITE "${source}" "")
foreach(part IN ITEMS
    "gói/biên dịch/bản địa/phân tích từ vựng.vi"
    "gói/biên dịch/bản địa/từ tố.vi"
    "gói/biên dịch/bản địa/phân tích cú pháp.vi"
    "gói/biên dịch/bản địa/chương trình.vi"
    "gói/biên dịch/bản địa/sinh mã.vi"
    "gói/biên dịch/bản địa/mã máy x86 64.vi"
    "gói/biên dịch/bản địa/sinh mã chương trình.vi"
    "gói/biên dịch/bản địa/đối tượng.vi"
    "gói/biên dịch/bản địa/đối tượng mô đun.vi"
    "gói/biên dịch/bản địa/định dạng elf 64.vi"
    "gói/biên dịch/bản địa/nạp mô đun.vi"
    "gói/biên dịch/bản địa/biên dịch mô đun.vi"
    "gói/biên dịch/bản địa/bộ nhớ đệm.vi"
    "gói/biên dịch/bản địa/trình điều khiển.vi")
    # Stage 0 only needs code for the host ISA. Once bootstrapped, Stage 1
    # resolves the complete, unchanged native/*.vi library for Stage 2.
    if (NOT stage0_linux AND (part MATCHES "/mã máy x86 64\\.vi$" OR
                              part MATCHES "/định dạng elf 64\\.vi$"))
        continue()
    endif()
    file(READ "${VPP_ROOT}/${part}" contents)
    if (stage0_linux)
        if (part MATCHES "/sinh mã\\.vi$")
            string(FIND "${contents}" "// MOVZ/MOVK W9" cut)
            if (cut LESS 0)
                message(FATAL_ERROR "cannot trim ARM64 codegen for Linux Stage 0")
            endif()
            string(SUBSTRING "${contents}" 0 ${cut} contents)
        elseif (part MATCHES "/sinh mã chương trình\\.vi$")
            string(FIND "${contents}" "// Linux AMD64 uses rel32" cut)
            if (cut LESS 0)
                message(FATAL_ERROR "cannot extract Linux program codegen")
            endif()
            string(SUBSTRING "${contents}" ${cut} -1 contents)
        elseif (part MATCHES "/đối tượng\\.vi$")
            string(FIND "${contents}" "// Returns byte length" cut)
            if (cut LESS 0)
                message(FATAL_ERROR "cannot trim Mach-O object writer")
            endif()
            string(SUBSTRING "${contents}" 0 ${cut} contents)
        elseif (part MATCHES "/đối tượng mô đun\\.vi$")
            string(FIND "${contents}" "// Objects for empty function modules" cut)
            if (cut LESS 0)
                message(FATAL_ERROR "cannot trim Mach-O module writer")
            endif()
            string(SUBSTRING "${contents}" 0 ${cut} contents)
        endif()
    elseif (part MATCHES "/sinh mã chương trình\\.vi$")
        string(FIND "${contents}" "// Linux AMD64 uses rel32" cut)
        if (cut LESS 0)
            message(FATAL_ERROR "cannot trim Linux program codegen for Darwin Stage 0")
        endif()
        string(SUBSTRING "${contents}" 0 ${cut} contents)
    endif()
    file(APPEND "${source}" "${contents}\n")
endforeach()
if (stage0_linux)
    file(APPEND "${source}" "\nhàm sinh mã chương trình phát mô đun(a,b,c,d,e,f,g,h) { trả về -1; };\n")
    file(APPEND "${source}" "hàm đối tượng mô đun macho mô đun(a,b,c,d,e,f,g,h,i) { trả về -1; };\n")
else()
    file(APPEND "${source}" "\nhàm sinh mã chương trình x86 phát mô đun(a,b,c,d,e,f,g,h) { trả về -1; };\n")
    file(APPEND "${source}" "hàm elf mô đun(a,b,c,d,e,f,g,h,i) { trả về -1; };\n")
endif()
if (NOT VPP_USE_PREPARED_COMPILER)
    file(REMOVE "${compiler}")
    message(STATUS "Stage 0 -> Stage 1: bootstrap standalone V++ native compiler")
    execute_process(COMMAND "${CMAKE_COMMAND}" -E env "VPP_HOME=${VPP_ROOT}"
        "VPP_ENABLE_MACHINE_JIT=1"
        "VPP_NATIVE_STAGE0_PROFILE=1"
        "VPP_NATIVE_PROFILE=1"
        "${VPP_EXEC}" build --target "${VPP_TARGET}" --emit-exe-selfhost
        "${source}" "${compiler}"
        WORKING_DIRECTORY "${VPP_OUTPUT_DIR}"
        RESULT_VARIABLE status OUTPUT_VARIABLE output ERROR_VARIABLE errors TIMEOUT 420)
    if (NOT status EQUAL 0 OR NOT EXISTS "${compiler}")
        message(FATAL_ERROR "native compiler Stage 1 build failed: ${status}\n${output}\n${errors}")
    endif()
elseif (NOT EXISTS "${compiler}")
    message(FATAL_ERROR "prepared native Stage 1 compiler missing: ${compiler}")
endif()
if (VPP_PREPARE_ONLY)
    message(STATUS "V++ native Stage 1 prepared: ${compiler}")
    return()
endif()

function(run_native_case name code expected)
    set(input "${VPP_OUTPUT_DIR}/${name}.vi")
    set(exe "${VPP_OUTPUT_DIR}/${name}")
    file(WRITE "${input}" "${code}\n")
    file(REMOVE "${exe}")
    execute_process(COMMAND "${compiler}" build "${input}" -o "${exe}"
        WORKING_DIRECTORY "${VPP_OUTPUT_DIR}"
        RESULT_VARIABLE status OUTPUT_VARIABLE output ERROR_VARIABLE errors TIMEOUT 60)
    if (NOT status EQUAL 0 OR NOT EXISTS "${exe}")
        message(FATAL_ERROR "Stage 1 cannot build ${name}: ${status}\n${output}\n${errors}")
    endif()
    execute_process(COMMAND "${exe}" RESULT_VARIABLE status TIMEOUT 30)
    if (NOT status EQUAL expected)
        message(FATAL_ERROR "Stage 1 ${name} expected ${expected}, got ${status}")
    endif()
endfunction()

run_native_case(program "hàm chính() { trả về 52; };" 52)
run_native_case(entry-main-alias "hàm main() { trả về 52; };" 52)
run_native_case(runtime-bytes "hàm chính() { biến b = byte mới(); byte thêm(b, 52); biến value = byte lấy(b, 0); byte hủy(b); trả về value; };" 52)
run_native_case(runtime-string "hàm chính() { biến b = \"Hello\"; trả về byte dài(b) + 47; };" 52)
run_native_case(runtime-utf8 "hàm chính() { biến b = \"Tiếng Việt\"; trả về byte dài(b) + 38; };" 52)
run_native_case(sub-order "hàm chính() { trả về 20 - (4 - 8); };" 24)
run_native_case(nested "hàm chính() { biến a = 9; biến b = 2; biến c = 5; trả về (a - b) * (b + c) - 1; };" 48)
run_native_case(logical-not "hàm chính() { biến x = -7; trả về 50 + !0 + !!x + !x + !!0; };" 52)
run_native_case(logical-not-condition "hàm chính() { biến x = 0; nếu (!x) { x = 52; }; trả về x; };" 52)
run_native_case(unary-positive "hàm chính() { trả về +(+52); };" 52)
run_native_case(boolean-literals "hàm chính() { trả về đúng * 50 + !sai + đúng; };" 52)
run_native_case(compound-assignment "hàm chính() { biến x = 4; x += 10; x *= 4; x -= 4; x++; x--; trả về x; };" 52)
run_native_case(loop-compound-add "hàm chính() { biến x = 0; lặp (i = 0; i < 26; i += 1) { x += 2; }; trả về x; };" 52)
run_native_case(loop-compound-sub "hàm chính() { biến x = 0; lặp (i = 52; i > 0; i -= 1) { x++; }; trả về x; };" 52)
run_native_case(loop-compound-mul "hàm chính() { biến x = 2; lặp (i = 1; i < 16; i *= 2) { x += 10; }; trả về x + 10; };" 52)
run_native_case(divide-signed "hàm chính() { trả về -159 / -3; };" 53)
run_native_case(remainder-signed "hàm chính() { trả về -159 % 52; };" 253)
run_native_case(divide-mod-compound "hàm chính() { biến x = -159; x /= -3; x %= 52; trả về x + 51; };" 52)
run_native_case(divide-overflow "hàm chính() { nếu (-2147483648 / -1 == -2147483648) { trả về 52; }; trả về 1; };" 52)
run_native_case(remainder-overflow "hàm chính() { nếu (-2147483648 % -1 == 0) { trả về 52; }; trả về 1; };" 52)
run_native_case(intrinsic-divide-signed "hàm chính() { trả về máy chia số nguyên ba mươi hai bit(-156, -3); };" 52)
run_native_case(intrinsic-divide-by-zero "hàm chính() { trả về máy chia số nguyên ba mươi hai bit(52, 0); };" 255)
run_native_case(intrinsic-divide-overflow "hàm chính() { nếu (máy chia số nguyên ba mươi hai bit(-2147483648, -1) == -2147483648) { trả về 52; }; trả về 1; };" 52)
run_native_case(min-int "hàm chính() { biến x = -2147483648; trả về -x + x + 31; };" 31)
run_native_case(max-int "hàm chính() { biến x = 2147483647; trả về (x + 1) - (-2147483648) + 63; };" 63)
run_native_case(wide-immediate "hàm chính() { biến x = 16777228; biến expected = 256 * 256 * 256 + 12; nếu (x == expected) { trả về 52; }; trả về 1; };" 52)
run_native_case(immediate-high16 "hàm chính() { biến x = 65536; nếu (x == 256 * 256) { trả về 52; }; trả về 1; };" 52)
run_native_case(negative-immediate "hàm chính() { biến x = -17958193; biến expected = 254 * 256 * 256 * 256 + 237 * 256 * 256 + 250 * 256 + 207; nếu (x == expected) { trả về 52; }; trả về 1; };" 52)
run_native_case(if-true "hàm chính() { biến x = 4; nếu (x < 5) { x = x + 48; } hoặc { x = 13; }; trả về x; };" 52)
run_native_case(if-false "hàm chính() { biến x = 4; nếu (x > 5) { x = 99; } hoặc { x = x + 48; }; trả về x; };" 52)
run_native_case(if-else-if "hàm chính() { biến x = 0; nếu (0) { x = 1; } hoặc nếu (1) { x = 52; } hoặc { x = 2; }; trả về x; };" 52)
run_native_case(if-else-if-return "hàm chính() { nếu (0) { trả về 1; } hoặc nếu (0) { trả về 2; } hoặc nếu (1) { trả về 52; } hoặc { trả về 3; }; };" 52)
run_native_case(if-no-else "hàm chính() { biến x = 52; nếu (0) { x = 8; }; trả về x; };" 52)
run_native_case(if-nested "hàm chính() { biến x = 1; nếu (x == 1) { nếu (x != 2) { x = 52; } hoặc { x = 1; }; } hoặc { x = 3; }; trả về x; };" 52)
run_native_case(if-early-true "hàm chính() { biến x = 52; nếu (x == 52) { trả về x; }; trả về 1; };" 52)
run_native_case(if-early-false "hàm chính() { biến x = 0; nếu (x != 0) { trả về 99; }; trả về 52; };" 52)
run_native_case(if-early-else "hàm chính() { biến x = 0; nếu (x == 1) { trả về 99; } hoặc { x = 52; }; trả về x; };" 52)
run_native_case(if-early-nested "hàm chính() { biến x = 52; nếu (x > 0) { nếu (x > 20) { trả về x; }; x = 1; }; trả về x; };" 52)
run_native_case(if-all-paths-return "hàm chính() { biến x = 52; nếu (x == 52) { trả về x; } hoặc { trả về 99; }; };" 52)
run_native_case(if-all-paths-nested "hàm chính() { biến x = 52; nếu (x > 0) { nếu (x > 10) { trả về x; } hoặc { trả về 1; }; } hoặc { trả về 2; }; };" 52)
run_native_case(if-local-true "hàm chính() { biến result = 0; nếu (1) { biến local = 26; result = local + 26; }; trả về result; };" 52)
run_native_case(if-local-else "hàm chính() { biến result = 0; nếu (0) { biến same = 1; result = same; } hoặc { biến same = 52; result = same; }; trả về result; };" 52)
run_native_case(if-local-nested "hàm chính() { biến result = 0; nếu (1) { biến outer = 2; nếu (1) { biến inner = 50; result = outer + inner; }; }; trả về result; };" 52)
run_native_case(ge-le "hàm chính() { biến x = -3; nếu (x <= -3) { x = 52; }; nếu (x >= 52) { x = x; }; trả về x; };" 52)
run_native_case(loop-five "hàm chính() { biến x = 2; lặp (i = 0; i < 5; i = i + 1) { x = x + 10; }; trả về x; };" 52)
run_native_case(loop-postincrement "hàm chính() { biến x = 0; lặp (i = 0; i < 52; i++) { x = x + 1; }; trả về x; };" 52)
run_native_case(loop-postdecrement "hàm chính() { biến x = 0; lặp (i = 52; i > 0; i--) { x = x + 1; }; trả về x; };" 52)
run_native_case(loop-redeclare "hàm chính() { biến x = 0; lặp (i = 0; i < 26; i++) { x = x + 1; }; lặp (i = 0; i < 26; i++) { x = x + 1; }; trả về x; };" 52)
run_native_case(loop-predeclared "hàm chính() { biến i = 0; lặp (i = 0; i < 52; i++) { }; trả về i; };" 52)
run_native_case(loop-zero "hàm chính() { biến x = 52; lặp (i = 0; i < 0; i = i + 1) { x = 8; }; trả về x; };" 52)
run_native_case(loop-nested "hàm chính() { biến x = 0; lặp (i = 0; i < 4; i = i + 1) { lặp (j = 0; j < 13; j = j + 1) { x = x + 1; }; }; trả về x; };" 52)
run_native_case(loop-early-return "hàm chính() { lặp (i = 0; i < 100; i = i + 1) { nếu (i == 52) { trả về i; }; }; trả về 1; };" 52)
run_native_case(loop-break "hàm chính() { biến n = 0; lặp (i = 0; i < 100; i = i + 1) { nếu (i == 52) { thoát; }; n = n + 1; }; trả về n; };" 52)
run_native_case(loop-continue "hàm chính() { biến n = 0; lặp (i = 0; i < 104; i = i + 1) { nếu (i < 52) { bỏ qua; }; n = n + 1; }; trả về n; };" 52)
run_native_case(loop-nested-break-continue "hàm chính() { biến n = 0; lặp (i = 0; i < 4; i = i + 1) { lặp (j = 0; j < 25; j = j + 1) { nếu (j < 10) { bỏ qua; }; nếu (j == 23) { thoát; }; n = n + 1; }; }; trả về n; };" 52)
run_native_case(call-forward "hàm chính() { trả về gấp_đôi() + 4; }; hàm gấp_đôi() { trả về 24 * 2; };" 52)
run_native_case(entry-last "hàm cộng(a,b) { trả về a+b; }; hàm chính() { trả về cộng(30,22); };" 52)
run_native_case(entry-middle "hàm trái() { trả về 26; }; hàm chính() { trả về trái() + phải(); }; hàm phải() { trả về 26; };" 52)
run_native_case(call-backward "hàm chính() { trả về thứ_nhất(); }; hàm thứ_nhất() { trả về thứ_hai() + 2; }; hàm thứ_hai() { trả về 50; };" 52)
run_native_case(call-nested "hàm chính() { biến x = 0; lặp (i = 0; i < 4; i = i + 1) { x = x + bước(); }; trả về x; }; hàm bước() { biến y = 2; nếu (y < 3) { y = y + 11; }; trả về y; };" 52)
run_native_case(arg-one "hàm chính() { trả về lấy(52); }; hàm lấy(a) { trả về a; };" 52)
run_native_case(arg-four "hàm chính() { trả về cộng(10, 12, 14, 16); }; hàm cộng(a,b,c,d) { trả về a+b+c+d; };" 52)
run_native_case(arg-five "hàm chính() { trả về cộng(1,2,3,4,42); }; hàm cộng(a,b,c,d,e) { trả về a+b+c+d+e; };" 52)
run_native_case(arg-eight "hàm chính() { trả về cộng(1,2,3,4,5,6,7,24); }; hàm cộng(a,b,c,d,e,f,g,h) { trả về a+b+c+d+e+f+g+h; };" 52)
run_native_case(arg-nine "hàm chính() { trả về cộng(1,2,3,4,5,6,7,8,16); }; hàm cộng(a,b,c,d,e,f,g,h,i) { trả về a+b+c+d+e+f+g+h+i; };" 52)
run_native_case(arg-sixteen "hàm chính() { trả về cuối(1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,52); }; hàm cuối(a,b,c,d,e,f,g,h,i,j,k,l,m,n,o,p) { trả về p; };" 52)
run_native_case(arg-nested "hàm chính() { trả về cộng(gấp(20),gấp(6)); }; hàm cộng(a,b) { trả về a+b; }; hàm gấp(a) { trả về a*2; };" 52)
run_native_case(arg-recursive "hàm chính() { trả về giai_thừa(5); }; hàm giai_thừa(n) { biến result = 1; nếu (n > 1) { result = n * giai_thừa(n - 1); }; trả về result; };" 120)

# Generated arity above the old parser/IR/stack limits, using the final arg.
set(large_parameters "")
set(large_arguments "")
foreach(i RANGE 0 127)
    if (i GREATER 0)
        string(APPEND large_parameters ",")
        string(APPEND large_arguments ",")
    endif()
    string(APPEND large_parameters "p${i}")
    if (i EQUAL 127)
        string(APPEND large_arguments "52")
    else()
        string(APPEND large_arguments "0")
    endif()
endforeach()
run_native_case(arg-128 "hàm chính() { trả về cuối(${large_arguments}); }; hàm cuối(${large_parameters}) { trả về p127; };" 52)

# Exercise function-table growth past the old 32-function ceiling. Both
# directions visit high function IDs and resolve calls across many bodies.
set(forward_chain "hàm chính() { trả về f1(); };\n")
set(backward_chain "")
foreach(i RANGE 1 63)
    if (i EQUAL 63)
        string(APPEND forward_chain "hàm f${i}() { trả về 52; };\n")
    else()
        math(EXPR next "${i} + 1")
        string(APPEND forward_chain "hàm f${i}() { trả về f${next}(); };\n")
    endif()
    if (i EQUAL 1)
        string(APPEND backward_chain "hàm f${i}() { trả về 52; };\n")
    else()
        math(EXPR prev "${i} - 1")
        string(APPEND backward_chain "hàm f${i}() { trả về f${prev}(); };\n")
    endif()
endforeach()
string(APPEND backward_chain "hàm chính() { trả về f63(); };\n")
run_native_case(functions-64-forward "${forward_chain}" 52)
run_native_case(functions-64-backward-entry-last "${backward_chain}" 52)

# Errors must never clobber previously published binaries or their input.
set(sentinel "${VPP_OUTPUT_DIR}/preserved")
file(WRITE "${sentinel}" "preserve-me")
file(WRITE "${VPP_OUTPUT_DIR}/bad.vi" "hàm chính() { trả về unknown; };\n")
execute_process(COMMAND "${compiler}" build "${VPP_OUTPUT_DIR}/bad.vi" -o "${sentinel}"
    WORKING_DIRECTORY "${VPP_OUTPUT_DIR}" RESULT_VARIABLE status TIMEOUT 30)
file(READ "${sentinel}" content)
if (status EQUAL 0 OR NOT content STREQUAL "preserve-me")
    message(FATAL_ERROR "native compiler did not preserve output on parse error: ${status}")
endif()
file(WRITE "${VPP_OUTPUT_DIR}/arity-invalid.vi"
    "hàm chính() { trả về lấy(1,2,3,4,5); }; hàm lấy(a,b,c,d,e,f) { trả về a; };\n")
execute_process(COMMAND "${compiler}" build "${VPP_OUTPUT_DIR}/arity-invalid.vi" -o "${sentinel}"
    WORKING_DIRECTORY "${VPP_OUTPUT_DIR}" RESULT_VARIABLE status TIMEOUT 30)
file(READ "${sentinel}" content)
if (status EQUAL 0 OR NOT content STREQUAL "preserve-me")
    message(FATAL_ERROR "native compiler accepted arity mismatch: ${status}")
endif()
foreach(entry IN ITEMS "hàm chính() { trả về 1; }; hàm main() { trả về 52; };"
                       "hàm main() { trả về 1; }; hàm chính() { trả về 52; };")
    file(WRITE "${VPP_OUTPUT_DIR}/duplicate-entry-alias.vi" "${entry}\n")
    execute_process(COMMAND "${compiler}" build
        "${VPP_OUTPUT_DIR}/duplicate-entry-alias.vi" -o "${sentinel}"
        WORKING_DIRECTORY "${VPP_OUTPUT_DIR}" RESULT_VARIABLE status TIMEOUT 30)
    file(READ "${sentinel}" content)
    if (status EQUAL 0 OR NOT content STREQUAL "preserve-me")
        message(FATAL_ERROR "native compiler accepted conflicting entrypoint aliases: ${status}")
    endif()
endforeach()
file(WRITE "${VPP_OUTPUT_DIR}/runtime-arity-invalid.vi"
    "hàm chính() { trả về byte lấy(1); };\n")
execute_process(COMMAND "${compiler}" build "${VPP_OUTPUT_DIR}/runtime-arity-invalid.vi" -o "${sentinel}"
    WORKING_DIRECTORY "${VPP_OUTPUT_DIR}" RESULT_VARIABLE status TIMEOUT 30)
file(READ "${sentinel}" content)
if (status EQUAL 0 OR NOT content STREQUAL "preserve-me")
    message(FATAL_ERROR "native compiler accepted runtime arity mismatch: ${status}")
endif()
# Duplicate declarations after the old table boundary must still be rejected.
set(duplicate_function "${forward_chain}hàm f60() { trả về 13; };\n")
file(WRITE "${VPP_OUTPUT_DIR}/duplicate-function.vi" "${duplicate_function}")
execute_process(COMMAND "${compiler}" build "${VPP_OUTPUT_DIR}/duplicate-function.vi" -o "${sentinel}"
    WORKING_DIRECTORY "${VPP_OUTPUT_DIR}" RESULT_VARIABLE status TIMEOUT 30)
file(READ "${sentinel}" content)
if (status EQUAL 0 OR NOT content STREQUAL "preserve-me")
    message(FATAL_ERROR "native compiler accepted duplicate function above index 32: ${status}")
endif()
file(WRITE "${VPP_OUTPUT_DIR}/branch-invalid.vi"
    "hàm chính() { nếu (1) { biến hidden = 52; }; trả về hidden; };\n")
execute_process(COMMAND "${compiler}" build "${VPP_OUTPUT_DIR}/branch-invalid.vi" -o "${sentinel}"
    WORKING_DIRECTORY "${VPP_OUTPUT_DIR}" RESULT_VARIABLE status TIMEOUT 30)
file(READ "${sentinel}" content)
if (status EQUAL 0 OR NOT content STREQUAL "preserve-me")
    message(FATAL_ERROR "out-of-scope branch-local reference must be rejected: ${status}")
endif()
file(WRITE "${VPP_OUTPUT_DIR}/loop-scope-invalid.vi"
    "hàm chính() { lặp (i = 0; i < 3; i++) { }; trả về i; };\n")
execute_process(COMMAND "${compiler}" build "${VPP_OUTPUT_DIR}/loop-scope-invalid.vi" -o "${sentinel}"
    WORKING_DIRECTORY "${VPP_OUTPUT_DIR}" RESULT_VARIABLE status TIMEOUT 30)
file(READ "${sentinel}" content)
if (status EQUAL 0 OR NOT content STREQUAL "preserve-me")
    message(FATAL_ERROR "out-of-scope loop variable must be rejected: ${status}")
endif()
file(WRITE "${VPP_OUTPUT_DIR}/unreachable-return.vi"
    "hàm chính() { nếu (1) { trả về 52; biến x = 1; }; trả về 0; };\n")
execute_process(COMMAND "${compiler}" build "${VPP_OUTPUT_DIR}/unreachable-return.vi" -o "${sentinel}"
    WORKING_DIRECTORY "${VPP_OUTPUT_DIR}" RESULT_VARIABLE status TIMEOUT 30)
file(READ "${sentinel}" content)
if (status EQUAL 0 OR NOT content STREQUAL "preserve-me")
    message(FATAL_ERROR "native compiler accepted unreachable statement after return: ${status}")
endif()
file(WRITE "${VPP_OUTPUT_DIR}/missing-return-path.vi"
    "hàm chính() { nếu (1) { trả về 52; }; };\n")
execute_process(COMMAND "${compiler}" build "${VPP_OUTPUT_DIR}/missing-return-path.vi" -o "${sentinel}"
    WORKING_DIRECTORY "${VPP_OUTPUT_DIR}" RESULT_VARIABLE status TIMEOUT 30)
file(READ "${sentinel}" content)
if (status EQUAL 0 OR NOT content STREQUAL "preserve-me")
    message(FATAL_ERROR "native compiler accepted missing-return control path: ${status}")
endif()
file(WRITE "${VPP_OUTPUT_DIR}/unreachable-after-if.vi"
    "hàm chính() { nếu (1) { trả về 52; } hoặc { trả về 51; }; trả về 0; };\n")
execute_process(COMMAND "${compiler}" build "${VPP_OUTPUT_DIR}/unreachable-after-if.vi" -o "${sentinel}"
    WORKING_DIRECTORY "${VPP_OUTPUT_DIR}" RESULT_VARIABLE status TIMEOUT 30)
file(READ "${sentinel}" content)
if (status EQUAL 0 OR NOT content STREQUAL "preserve-me")
    message(FATAL_ERROR "native compiler accepted unreachable code after returning branches: ${status}")
endif()
file(WRITE "${VPP_OUTPUT_DIR}/break-outside-loop.vi"
    "hàm chính() { thoát; trả về 52; };\n")
execute_process(COMMAND "${compiler}" build "${VPP_OUTPUT_DIR}/break-outside-loop.vi" -o "${sentinel}"
    WORKING_DIRECTORY "${VPP_OUTPUT_DIR}" RESULT_VARIABLE status TIMEOUT 30)
file(READ "${sentinel}" content)
if (status EQUAL 0 OR NOT content STREQUAL "preserve-me")
    message(FATAL_ERROR "native compiler accepted break outside loop: ${status}")
endif()
file(WRITE "${VPP_OUTPUT_DIR}/continue-outside-loop.vi"
    "hàm chính() { bỏ qua; trả về 52; };\n")
execute_process(COMMAND "${compiler}" build "${VPP_OUTPUT_DIR}/continue-outside-loop.vi" -o "${sentinel}"
    WORKING_DIRECTORY "${VPP_OUTPUT_DIR}" RESULT_VARIABLE status TIMEOUT 30)
file(READ "${sentinel}" content)
if (status EQUAL 0 OR NOT content STREQUAL "preserve-me")
    message(FATAL_ERROR "native compiler accepted continue outside loop: ${status}")
endif()
set(input "${VPP_OUTPUT_DIR}/program.vi")
file(READ "${input}" before)
execute_process(COMMAND "${compiler}" build "${input}" -o "${input}"
    WORKING_DIRECTORY "${VPP_OUTPUT_DIR}" RESULT_VARIABLE status TIMEOUT 30)
file(READ "${input}" after)
if (status EQUAL 0 OR NOT before STREQUAL after)
    message(FATAL_ERROR "native compiler clobbered its own source: ${status}")
endif()

# Resolve real V++ module files from the importing module's directory.
# Repeated imports must not introduce duplicate function definitions.
set(module_dir "${VPP_OUTPUT_DIR}/native-module-fixtures")
file(MAKE_DIRECTORY "${module_dir}/sub")
file(WRITE "${module_dir}/sub/leaf.vi" "hàm gốc() { trả về 40; };\n")
file(WRITE "${module_dir}/sub/child.vi"
    "nhập leaf;\nhàm giữa() { trả về gốc() + 10; };\n")
file(WRITE "${module_dir}/main.vi"
    "nhập sub/child;\nnhập sub/leaf;\nnhập sub/child;\nhàm chính() { trả về giữa() + 2; };\n")
set(module_program "${module_dir}/module-main")
file(REMOVE "${module_program}")
execute_process(COMMAND "${compiler}" build "${module_dir}/main.vi" -o "${module_program}"
    RESULT_VARIABLE status OUTPUT_VARIABLE output ERROR_VARIABLE errors TIMEOUT 60)
if (NOT status EQUAL 0 OR NOT EXISTS "${module_program}")
    message(FATAL_ERROR "native nested/repeated module imports failed: ${status}\n${output}\n${errors}")
endif()
execute_process(COMMAND "${module_program}" RESULT_VARIABLE status TIMEOUT 30)
if (NOT status EQUAL 52)
    message(FATAL_ERROR "native nested/repeated module imports expected 52, got ${status}")
endif()

# Cross-module native relocations must resolve in both directions, including
# calls to a declaration in the importer that appears after the import.
file(WRITE "${module_dir}/sub/forward.vi"
    "hàm từ_phụ() { biến x = \"Tiếng Việt\"; biến n = byte dài(x); byte hủy(x); trả về từ_chính() + n - 14; };\n")
file(WRITE "${module_dir}/forward.vi"
    "nhập sub/forward;\nhàm chính() { trả về từ_phụ(); };\nhàm từ_chính() { trả về 52; };\n")
set(forward_program "${module_dir}/forward-exe")
file(REMOVE "${forward_program}")
execute_process(COMMAND "${compiler}" build "${module_dir}/forward.vi" -o "${forward_program}"
    RESULT_VARIABLE status OUTPUT_VARIABLE output ERROR_VARIABLE errors TIMEOUT 60)
if (NOT status EQUAL 0 OR NOT EXISTS "${forward_program}")
    message(FATAL_ERROR "native reverse cross-module relocation failed: ${status}\n${output}\n${errors}")
endif()
execute_process(COMMAND "${forward_program}" RESULT_VARIABLE status TIMEOUT 30)
if (NOT status EQUAL 52)
    message(FATAL_ERROR "native reverse cross-module relocation expected 52, got ${status}")
endif()

# The linker must accept more than 32 independent native objects. Each
# dependency contributes one function and the entrypoint calls all of them.
set(many_imports "")
set(many_sum "18")
foreach(module_index RANGE 0 33)
    file(WRITE "${module_dir}/many${module_index}.vi"
        "hàm many_fn${module_index}() { trả về 1; };\n")
    string(APPEND many_imports "nhập many${module_index};\n")
    string(APPEND many_sum " + many_fn${module_index}()")
endforeach()
file(WRITE "${module_dir}/many-main.vi"
    "${many_imports}hàm chính() { trả về ${many_sum}; };\n")
set(many_program "${module_dir}/many-exe")
file(REMOVE "${many_program}")
execute_process(COMMAND "${compiler}" build "${module_dir}/many-main.vi" -o "${many_program}"
    RESULT_VARIABLE status OUTPUT_VARIABLE output ERROR_VARIABLE errors TIMEOUT 120)
if (NOT status EQUAL 0 OR NOT EXISTS "${many_program}")
    message(FATAL_ERROR "native 35-object link failed: ${status}\n${output}\n${errors}")
endif()
execute_process(COMMAND "${many_program}" RESULT_VARIABLE status TIMEOUT 30)
if (NOT status EQUAL 52)
    message(FATAL_ERROR "native 35-object link expected 52, got ${status}")
endif()

# An import-only entrypoint should find `chính` in its dependencies.
file(WRITE "${module_dir}/sub/entry.vi" "hàm chính() { trả về 52; };\n")
file(WRITE "${module_dir}/import-only.vi" "nhập sub/entry;\n")
set(import_only "${module_dir}/import-only-exe")
file(REMOVE "${import_only}")
execute_process(COMMAND "${compiler}" build "${module_dir}/import-only.vi" -o "${import_only}"
    RESULT_VARIABLE status OUTPUT_VARIABLE output ERROR_VARIABLE errors TIMEOUT 60)
if (NOT status EQUAL 0 OR NOT EXISTS "${import_only}")
    message(FATAL_ERROR "native import-only entrypoint failed: ${status}\n${output}\n${errors}")
endif()
execute_process(COMMAND "${import_only}" RESULT_VARIABLE status TIMEOUT 30)
if (NOT status EQUAL 52)
    message(FATAL_ERROR "native import-only entrypoint expected 52, got ${status}")
endif()

# Import failures must return an error and leave published outputs untouched.
file(WRITE "${module_dir}/cycle_a.vi" "nhập cycle_b;\nhàm chính() { trả về 0; };\n")
file(WRITE "${module_dir}/cycle_b.vi" "nhập cycle_a;\n")
file(WRITE "${module_dir}/missing.vi" "nhập absent_module;\nhàm chính() { trả về 0; };\n")
file(WRITE "${module_dir}/malformed.vi" "nhập sub/;\nhàm chính() { trả về 0; };\n")
file(WRITE "${module_dir}/duplicate.vi"
    "nhập sub/leaf;\nhàm gốc() { trả về 1; };\nhàm chính() { trả về gốc(); };\n")
file(WRITE "${module_dir}/arity.vi"
    "nhập sub/leaf;\nhàm chính() { trả về gốc(12); };\n")
file(WRITE "${module_dir}/undefined.vi"
    "nhập sub/leaf;\nhàm chính() { trả về không_tồn_tại(); };\n")
file(WRITE "${module_dir}/no-main.vi"
    "nhập sub/leaf;\nhàm trợ_giúp() { trả về gốc(); };\n")
foreach(failed IN ITEMS cycle_a missing malformed duplicate arity undefined no-main)
    execute_process(COMMAND "${compiler}" build "${module_dir}/${failed}.vi" -o "${sentinel}"
        RESULT_VARIABLE status OUTPUT_VARIABLE output ERROR_VARIABLE errors TIMEOUT 30)
    file(READ "${sentinel}" content)
    if (status EQUAL 0 OR NOT content STREQUAL "preserve-me")
        message(FATAL_ERROR "native module loader accepted ${failed} or clobbered output: ${status}\n${output}\n${errors}")
    endif()
endforeach()

# Native self-hosting: the compiler executable emitted by Stage 0 must be
# able to compile the original, multi-file V++ sources through three generations.
set(stage2 "${VPP_OUTPUT_DIR}/vpp-native-stage2")
set(stage3 "${VPP_OUTPUT_DIR}/vpp-native-stage3")
set(stage4 "${VPP_OUTPUT_DIR}/vpp-native-stage4")
file(REMOVE "${stage2}" "${stage3}" "${stage4}")
message(STATUS "Stage 1 -> Stage 2: compile original multi-module compiler")
execute_process(COMMAND "${compiler}" build "${module_entry}" -o "${stage2}"
    WORKING_DIRECTORY "${VPP_OUTPUT_DIR}"
    RESULT_VARIABLE status OUTPUT_VARIABLE output ERROR_VARIABLE errors TIMEOUT 180)
if (NOT status EQUAL 0 OR NOT EXISTS "${stage2}")
    message(FATAL_ERROR "native self-host Stage 1 -> Stage 2 failed: ${status}\n${output}\n${errors}")
endif()
message(STATUS "Stage 2 -> Stage 3: self-compile native compiler")
execute_process(COMMAND "${stage2}" build "${module_entry}" -o "${stage3}"
    WORKING_DIRECTORY "${VPP_OUTPUT_DIR}"
    RESULT_VARIABLE status OUTPUT_VARIABLE output ERROR_VARIABLE errors TIMEOUT 180)
if (NOT status EQUAL 0 OR NOT EXISTS "${stage3}")
    message(FATAL_ERROR "native self-host Stage 2 -> Stage 3 failed: ${status}\n${output}\n${errors}")
endif()
message(STATUS "Stage 3 -> Stage 4: self-compile native compiler")
execute_process(COMMAND "${stage3}" build "${module_entry}" -o "${stage4}"
    WORKING_DIRECTORY "${VPP_OUTPUT_DIR}"
    RESULT_VARIABLE status OUTPUT_VARIABLE output ERROR_VARIABLE errors TIMEOUT 180)
if (NOT status EQUAL 0 OR NOT EXISTS "${stage4}")
    message(FATAL_ERROR "native self-host Stage 3 -> Stage 4 failed: ${status}\n${output}\n${errors}")
endif()
message(STATUS "Stage 2/3/4: verify machine-code parity and execute generation probes")

# Compare machine sections; UUIDs and signatures can differ between builds.
execute_process(COMMAND "${CMAKE_COMMAND}"
    "-DVPP_STAGE2=${stage2}" "-DVPP_STAGE3=${stage3}" "-DVPP_STAGE4=${stage4}"
    -P "${VPP_ROOT}/scripts/quality/check_native_bootstrap_text.cmake"
    RESULT_VARIABLE parity_status OUTPUT_VARIABLE parity_output ERROR_VARIABLE parity_errors TIMEOUT 30)
if (NOT parity_status EQUAL 0)
    message(FATAL_ERROR "Native bootstrap machine-code parity failed: ${parity_status}\n${parity_output}\n${parity_errors}")
endif()
message(STATUS "${parity_output}")

# Probe all self-hosted generations; the high-word immediate catches instruction
# encoding regressions, alongside UTF-8 bytes, calls and loop updates.
set(bootstrap_probe "${VPP_OUTPUT_DIR}/native-bootstrap-probe.vi")
file(WRITE "${bootstrap_probe}" "hàm chính() { biến x = 16777228; biến y = 256 * 256 * 256 + 12; nếu (x != y) { trả về 1; }; nếu (máy chia số nguyên ba mươi hai bit(-156, -3) != 52) { trả về 2; }; nếu (máy chia số nguyên ba mươi hai bit(-2147483648, -1) != -2147483648) { trả về 3; }; nếu (máy chia số nguyên ba mươi hai bit(52, 0) != -1) { trả về 4; }; nếu (-159 % 52 != -3) { trả về 5; }; biến s = \"Tiếng Việt\"; biến total = byte dài(s) + 38; byte hủy(s); lặp (i = 0; i < 3; i++) { total = total + 1; }; trả về total - 3; };\n")
foreach(generation IN ITEMS 2 3 4)
    set(generation_compiler "${stage${generation}}")
    set(probe_exe "${VPP_OUTPUT_DIR}/native-bootstrap-probe-stage${generation}")
    file(REMOVE "${probe_exe}")
    execute_process(COMMAND "${generation_compiler}" build "${bootstrap_probe}" -o "${probe_exe}"
        WORKING_DIRECTORY "${VPP_OUTPUT_DIR}"
        RESULT_VARIABLE status OUTPUT_VARIABLE output ERROR_VARIABLE errors TIMEOUT 60)
    if (NOT status EQUAL 0 OR NOT EXISTS "${probe_exe}")
        message(FATAL_ERROR "native Stage ${generation} bootstrap probe build failed: ${status}\n${output}\n${errors}")
    endif()
    execute_process(COMMAND "${probe_exe}" RESULT_VARIABLE status TIMEOUT 30)
    if (NOT status EQUAL 52)
        message(FATAL_ERROR "native Stage ${generation} bootstrap probe expected 52, got ${status}")
    endif()
endforeach()
message(STATUS "V++ native compiler Stage 1 -> Stage 2 -> Stage 3 -> Stage 4, code parity and runtime probes: PASS")
