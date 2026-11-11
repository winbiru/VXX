# Verify compiler-emitted ASM is accepted by Clang's real assembler.
if (NOT DEFINED VPP_EXEC OR NOT DEFINED VPP_CLANG OR NOT DEFINED VPP_LEAF OR
    NOT DEFINED VPP_GENERAL OR NOT DEFINED VPP_OUTPUT_DIR)
    message(FATAL_ERROR "Native ASM: missing inputs")
endif()
file(MAKE_DIRECTORY "${VPP_OUTPUT_DIR}")
foreach(case IN ITEMS
    "i386-unknown-linux-gnu#${VPP_LEAF}#tra42"
    "x86_64-unknown-linux-gnu#${VPP_LEAF}#cong"
    "x86_64-unknown-linux-gnu#${VPP_LEAF}#tra_i32_am"
    "aarch64-unknown-linux-gnu#${VPP_LEAF}#tru"
    "aarch64-apple-darwin#${VPP_LEAF}#tra42"
    "aarch64-apple-darwin#${VPP_LEAF}#tra_i32_am"
    "aarch64-apple-darwin#${VPP_LEAF}#cong"
    "aarch64-apple-darwin#${VPP_LEAF}#tru"
    "aarch64-apple-darwin#${VPP_LEAF}#hang_chia"
    "x86_64-pc-windows-msvc#${VPP_LEAF}#cong"
    "aarch64-unknown-linux-gnu#${VPP_GENERAL}#tong"
    "x86_64-unknown-linux-gnu#${VPP_GENERAL}#tinh")
    string(REPLACE "#" ";" parts "${case}")
    list(GET parts 0 target)
    list(GET parts 1 source)
    list(GET parts 2 name)
    set(asm "${VPP_OUTPUT_DIR}/${target}-${name}.s")
    set(obj "${VPP_OUTPUT_DIR}/${target}-${name}.o")
    execute_process(COMMAND "${VPP_EXEC}" build --target "${target}" --emit-asm
        "${name}" "${source}" "${asm}" RESULT_VARIABLE status ERROR_VARIABLE errors TIMEOUT 90)
    if (NOT status EQUAL 0)
        message(FATAL_ERROR "V++ ASM emit failed for ${target}/${name}: ${errors}")
    endif()
    execute_process(COMMAND "${VPP_CLANG}" -target "${target}" -c "${asm}" -o "${obj}"
        RESULT_VARIABLE status ERROR_VARIABLE errors TIMEOUT 90)
    if (NOT status EQUAL 0 OR NOT EXISTS "${obj}")
        message(FATAL_ERROR "Clang rejected V++ ASM ${target}/${name}: ${errors}")
    endif()
endforeach()

# Dynamic AOT bodies bypass the byte encoder. Assemble their actual mnemonic
# output for every supported ABI, including stack arguments and a CFG join.
get_filename_component(test_sources "${VPP_LEAF}" DIRECTORY)
execute_process(COMMAND "${CMAKE_COMMAND}" -E env "VPP_DIRECT_ASM_TEST_DIR=${VPP_OUTPUT_DIR}"
    "${VPP_EXEC}" run --compiler=cpp
    "${test_sources}/fixtures/native_direct_asm.vi"
    WORKING_DIRECTORY "${VPP_OUTPUT_DIR}"
    RESULT_VARIABLE status OUTPUT_VARIABLE output ERROR_VARIABLE errors TIMEOUT 90)
if(NOT status EQUAL 0)
    message(FATAL_ERROR "Direct ASM fixture failed: ${output}\n${errors}")
endif()
foreach(case IN ITEMS
    "aarch64-apple-darwin#aarch64-macos"
    "aarch64-unknown-linux-gnu#aarch64-linux"
    "x86_64-apple-darwin#x86_64-macos"
    "x86_64-unknown-linux-gnu#x86_64-linux"
    "x86_64-pc-windows-msvc#x86_64-windows")
    string(REPLACE "#" ";" parts "${case}")
    list(GET parts 0 target)
    list(GET parts 1 name)
    execute_process(COMMAND "${VPP_CLANG}" -target "${target}" -c
        "${VPP_OUTPUT_DIR}/direct-${name}.s" -o "${VPP_OUTPUT_DIR}/direct-${name}.o"
        RESULT_VARIABLE status ERROR_VARIABLE errors TIMEOUT 90)
    if(NOT status EQUAL 0)
        message(FATAL_ERROR "Clang rejected direct ASM ${target}: ${errors}")
    endif()
endforeach()
set(direct_host "")
if(NOT DEFINED VPP_HOST_PROCESSOR)
    set(VPP_HOST_PROCESSOR "${CMAKE_HOST_SYSTEM_PROCESSOR}")
endif()
if(CMAKE_HOST_SYSTEM_NAME STREQUAL "Darwin" AND
   VPP_HOST_PROCESSOR MATCHES "^(arm64|aarch64)$")
    set(direct_host "aarch64-macos")
elseif(CMAKE_HOST_SYSTEM_NAME STREQUAL "Linux")
    if(VPP_HOST_PROCESSOR MATCHES "^(arm64|aarch64)$")
        set(direct_host "aarch64-linux")
    elseif(VPP_HOST_PROCESSOR MATCHES "^(amd64|x86_64)$")
        set(direct_host "x86_64-linux")
    endif()
elseif(CMAKE_HOST_SYSTEM_NAME STREQUAL "Windows")
    set(direct_host "x86_64-windows")
endif()
if(NOT direct_host STREQUAL "")
    set(harness "${VPP_OUTPUT_DIR}/direct-asm-harness.c")
    file(WRITE "${harness}" [=[
int direct_probe9(int); int direct_branch(int); int direct_large_frame(int);
int direct_status(int); int direct_identity(int a) { return a; }
int direct_spec_add(int,int); int direct_spec_arg9(int,int,int,int,int,int,int,int,int);
int direct_spec_div(int,int); int direct_spec_mod(int,int); int direct_spec_call(int,int);
int direct_leaf_eight(int,int,int,int,int,int,int,int);
int direct_leaf_branch(int); int direct_leaf_mod(int); int direct_leaf_mod_fallback(int);
int direct_leaf_store(int);
int direct_leaf_and(int,int); int direct_leaf_or(int,int); int direct_leaf_xor(int,int);
int direct_leaf_shl(int,int); int direct_leaf_shr(int,int);
int direct_leaf_eq(int,int); int direct_leaf_ne(int,int);
int direct_leaf_lt(int,int); int direct_leaf_le(int,int);
int direct_leaf_gt(int,int); int direct_leaf_ge(int,int);
int direct_leaf_sub(int,int); int direct_leaf_mul(int,int);
int direct_sum2(int a,int b) { return a+b; }
int direct_sum9(int a,int b,int c,int d,int e,int f,int g,int h,int i) {
    return a+b+c+d+e+f+g+h+i;
}
int main(void) {
    if(direct_probe9(1)!=45 || direct_probe9(-5)!=39) return 1;
    if(direct_branch(0)!=23 || direct_branch(1)!=13) return 2;
    if(direct_large_frame(-123)!=-123) return 3;
    if(direct_status(0)!=9 || direct_status(-1)!=-9) return 4;
    if(direct_spec_add(-7,42)!=35 || direct_spec_call(40,2)!=42) return 5;
    if(direct_spec_arg9(1,2,3,4,5,6,7,8,99)!=99) return 6;
    if(direct_spec_div(-7,2)!=-3 || direct_spec_mod(-7,2)!=-1) return 7;
    if(direct_spec_div(7,0)!=-1 || direct_spec_mod(7,0)!=-1) return 8;
    if(direct_spec_div((-2147483647-1),-1)!=(-2147483647-1)) return 9;
    if(direct_spec_mod((-2147483647-1),-1)!=0) return 10;
    if(direct_leaf_eight(1,2,3,4,5,6,7,8)!=36) return 11;
    if(direct_leaf_eight(-1,-2,-3,-4,-5,-6,-7,-8)!=-36) return 12;
    if(direct_leaf_branch(0)!=23 || direct_leaf_branch(1)!=13) return 13;
    if(direct_leaf_mod(-7)!=-1 || direct_leaf_mod_fallback(-7)!=-1) return 14;
    if(direct_leaf_mod(7)!=1 || direct_leaf_mod_fallback(7)!=1) return 15;
    if(direct_leaf_store(-7)!=-14 || direct_leaf_store(7)!=14) return 16;
    if(direct_leaf_and(6,3)!=2 || direct_leaf_or(6,3)!=7 ||
       direct_leaf_xor(6,3)!=5) return 17;
    if(direct_leaf_shl(3,2)!=12 || direct_leaf_shr(-16,2)!=-4) return 18;
    if(direct_leaf_eq(4,4)!=1 || direct_leaf_eq(4,5)!=0 ||
       direct_leaf_ne(4,4)!=0 || direct_leaf_ne(4,5)!=1) return 19;
    if(direct_leaf_lt(4,5)!=1 || direct_leaf_lt(5,4)!=0 ||
       direct_leaf_le(4,4)!=1 || direct_leaf_le(5,4)!=0) return 20;
    if(direct_leaf_gt(5,4)!=1 || direct_leaf_gt(4,5)!=0 ||
       direct_leaf_ge(4,4)!=1 || direct_leaf_ge(4,5)!=0) return 21;
    if(direct_leaf_sub(7,9)!=-2 || direct_leaf_mul(7,-9)!=-63) return 22;
    return 0;
}
]=])
    set(executable "${VPP_OUTPUT_DIR}/direct-asm-harness")
    if(CMAKE_HOST_SYSTEM_NAME STREQUAL "Windows")
        string(APPEND executable ".exe")
    endif()
    execute_process(COMMAND "${VPP_CLANG}" "${harness}"
        "${VPP_OUTPUT_DIR}/direct-${direct_host}.o" -o "${executable}"
        RESULT_VARIABLE status ERROR_VARIABLE errors TIMEOUT 90)
    if(NOT status EQUAL 0)
        message(FATAL_ERROR "Direct ASM host link failed: ${errors}")
    endif()
    execute_process(COMMAND "${executable}" RESULT_VARIABLE status ERROR_VARIABLE errors TIMEOUT 30)
    if(NOT status EQUAL 0)
        message(FATAL_ERROR "Direct ASM ABI/CFG/large frame mismatch (${status}): ${errors}")
    endif()
endif()

# Check that the ARM64 leaf mnemonic printer agrees with the machine ABI,
# including signed i32 boundaries. Run the assembled V++ functions on host.
if (CMAKE_HOST_SYSTEM_NAME STREQUAL "Darwin" AND
    VPP_HOST_PROCESSOR MATCHES "^(arm64|aarch64)$")
    set(leaf_prefix "${VPP_OUTPUT_DIR}/aarch64-apple-darwin")
    set(harness "${VPP_OUTPUT_DIR}/native-leaf-asm-smoke.c")
    set(executable "${VPP_OUTPUT_DIR}/native-leaf-asm-smoke")
    file(WRITE "${harness}" "#include <stdint.h>\n"
        "int tra42(void);\nint tra_i32_am(void);\n"
        "int cong(int, int);\nint tru(int, int);\nint hang_chia(void);\n"
        "int main(void) {\n"
        "  if (tra42() != 42 || tra_i32_am() != INT32_MIN) return 1;\n"
        "  if (cong(-7, 42) != 35 || cong(INT32_MIN, 0) != INT32_MIN) return 2;\n"
        "  if (tru(42, -7) != 49 || tru(INT32_MIN, 0) != INT32_MIN) return 3;\n"
        "  if (hang_chia() != 5) return 4;\n"
        "  return 0;\n}\n")
    execute_process(COMMAND "${VPP_CLANG}" -target aarch64-apple-darwin
        "${harness}" "${leaf_prefix}-tra42.o" "${leaf_prefix}-tra_i32_am.o"
        "${leaf_prefix}-cong.o" "${leaf_prefix}-tru.o"
        "${leaf_prefix}-hang_chia.o" -o "${executable}"
        RESULT_VARIABLE status ERROR_VARIABLE errors TIMEOUT 90)
    if (NOT status EQUAL 0)
        message(FATAL_ERROR "V++ ARM64 leaf ASM host link failed: ${errors}")
    endif()
    execute_process(COMMAND "${executable}"
        RESULT_VARIABLE status ERROR_VARIABLE errors TIMEOUT 30)
    if (NOT status EQUAL 0)
        message(FATAL_ERROR "V++ ARM64 leaf ASM runtime mismatch (status ${status}): ${errors}")
    endif()
endif()
message(STATUS "V++ Assembly accepted by Clang; ARM64 host leaf execution: PASS (where supported)")
