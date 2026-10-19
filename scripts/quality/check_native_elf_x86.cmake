# Cross-host smoke: compile the V++ encoder into a native host executable,
# then emit a real ELF64 x86-64 object, even on macOS ARM64.
if (NOT DEFINED VPP_EXEC OR NOT DEFINED VPP_ROOT OR NOT DEFINED VPP_TARGET OR
    NOT DEFINED VPP_OUTPUT_DIR)
    message(FATAL_ERROR "missing native ELF cross-host smoke arguments")
endif()
file(MAKE_DIRECTORY "${VPP_OUTPUT_DIR}")
set(source "${VPP_OUTPUT_DIR}/elf-x86-harness.vi")
set(harness "${VPP_OUTPUT_DIR}/elf-x86-harness")
set(object "${VPP_OUTPUT_DIR}/elf-x86-output.o")
set(relocation_object "${VPP_OUTPUT_DIR}/elf-x86-relocation.o")
file(WRITE "${source}" "")
foreach(part IN ITEMS
    "gói/biên dịch/native/codegen.vi"
    "gói/biên dịch/native/x86_64.vi"
    "gói/biên dịch/native/object.vi"
    "gói/biên dịch/native/module_object.vi"
    "gói/biên dịch/native/elf64.vi"
    "src/tests/native_elf_x86_harness.vi")
    file(READ "${VPP_ROOT}/${part}" contents)
    file(APPEND "${source}" "${contents}\n")
endforeach()
file(REMOVE "${harness}" "${object}" "${relocation_object}")
execute_process(COMMAND "${CMAKE_COMMAND}" -E env
    "VPP_HOME=${VPP_ROOT}"
    "${VPP_EXEC}" build --target "${VPP_TARGET}" --emit-exe-selfhost
    "${source}" "${harness}"
    WORKING_DIRECTORY "${VPP_OUTPUT_DIR}"
    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE errors TIMEOUT 240)
if (NOT result EQUAL 0 OR NOT EXISTS "${harness}")
    message(FATAL_ERROR "cross-host V++ ELF harness build: ${result}\n${output}\n${errors}")
endif()
execute_process(COMMAND "${harness}" "${object}" "${relocation_object}"
    WORKING_DIRECTORY "${VPP_OUTPUT_DIR}"
    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE errors TIMEOUT 45)
if (NOT result EQUAL 0 OR NOT EXISTS "${object}" OR NOT EXISTS "${relocation_object}")
    message(FATAL_ERROR "V++ x86-64 ELF writer: ${result}\n${output}\n${errors}")
endif()
file(READ "${object}" header HEX LIMIT 20)
if (NOT header MATCHES "^7f454c4602010100000000000000000001003e00$")
    message(FATAL_ERROR "invalid ELF64 x86-64 ET_REL header: ${header}")
endif()
file(READ "${relocation_object}" relocation_header HEX LIMIT 20)
if (NOT relocation_header STREQUAL header)
    message(FATAL_ERROR "invalid ELF64 relocation-object header: ${relocation_header}")
endif()
file(READ "${relocation_object}" relocation_hex HEX)
if (NOT relocation_hex MATCHES "7670705f6e61746976655f6172676300")
    message(FATAL_ERROR "ELF64 relocation object missing vpp_native_argc symbol")
endif()
if (CMAKE_HOST_SYSTEM_NAME STREQUAL "Linux" AND
    CMAKE_HOST_SYSTEM_PROCESSOR MATCHES "^(x86_64|amd64)$")
    find_program(VPP_ELF_CC NAMES cc gcc clang REQUIRED)
    execute_process(COMMAND "${VPP_ELF_CC}" "${object}" -o "${VPP_OUTPUT_DIR}/elf-x86-result"
        RESULT_VARIABLE result ERROR_VARIABLE errors TIMEOUT 45)
    if (NOT result EQUAL 0)
        message(FATAL_ERROR "ELF link failed: ${result}\n${errors}")
    endif()
    execute_process(COMMAND "${VPP_OUTPUT_DIR}/elf-x86-result"
        RESULT_VARIABLE result TIMEOUT 30)
    if (NOT result EQUAL 52)
        message(FATAL_ERROR "V++ ELF x86-64 executable should return 52, got ${result}")
    endif()
    file(WRITE "${VPP_OUTPUT_DIR}/elf-x86-runtime-stub.c"
        "int vpp_native_argc(int a, int b, int c, int d, int e, int f, int g, int h) { return a + b + c + d + e + f + g + h; }\n")
    execute_process(COMMAND "${VPP_ELF_CC}" "${relocation_object}"
        "${VPP_OUTPUT_DIR}/elf-x86-runtime-stub.c"
        -o "${VPP_OUTPUT_DIR}/elf-x86-relocation-result"
        RESULT_VARIABLE result ERROR_VARIABLE errors TIMEOUT 45)
    if (NOT result EQUAL 0)
        message(FATAL_ERROR "ELF PLT32 runtime-symbol link failed: ${result}\n${errors}")
    endif()
    execute_process(COMMAND "${VPP_OUTPUT_DIR}/elf-x86-relocation-result"
        RESULT_VARIABLE result TIMEOUT 30)
    if (NOT result EQUAL 52)
        message(FATAL_ERROR "ELF PLT32 runtime call should return 52, got ${result}")
    endif()

    # Link the same V++ machine-code artifact against the actual Linux runtime.
    # The stub above checks all eight SysV arguments; here the real argc()
    # ignores those arguments and must observe one process argument.
    execute_process(COMMAND "${VPP_ELF_CC}" "${relocation_object}"
        "${VPP_ROOT}/src/runtime/native/vpp_aot_runtime.c" -lcrypto
        -o "${VPP_OUTPUT_DIR}/elf-x86-runtime-result"
        RESULT_VARIABLE result ERROR_VARIABLE errors TIMEOUT 45)
    if (NOT result EQUAL 0)
        message(FATAL_ERROR "Linux ELF C runtime link failed: ${result}\n${errors}")
    endif()
    execute_process(COMMAND "${VPP_OUTPUT_DIR}/elf-x86-runtime-result"
        RESULT_VARIABLE result TIMEOUT 30)
    if (NOT result EQUAL 1)
        message(FATAL_ERROR "Linux native argc via ELF PLT32 expected 1, got ${result}")
    endif()

    # Verify the Linux /proc argv ABI and versioned SHA-256 artifact cache.
    # A second process must recover the bytes published by the first one;
    # appending garbage must invalidate the framed cache record.
    set(cache_source "${VPP_OUTPUT_DIR}/elf-x86-cache-smoke.c")
    set(cache_exe "${VPP_OUTPUT_DIR}/elf-x86-cache-smoke")
    set(cache_dir "${VPP_OUTPUT_DIR}/linux-cache-smoke")
    file(WRITE "${cache_source}" [=[
#include <stdint.h>
#include <string.h>
extern int32_t vpp_native_argc(void);
extern int32_t vpp_native_argv(int32_t);
extern int32_t vpp_native_bytes_new(void);
extern int32_t vpp_native_bytes_append(int32_t, int32_t);
extern int32_t vpp_native_bytes_length(int32_t);
extern int32_t vpp_native_bytes_get(int32_t, int32_t);
extern int32_t vpp_native_bytes_free(int32_t);
extern int32_t vpp_native_cache_read(int32_t, int32_t);
extern int32_t vpp_native_cache_write(int32_t, int32_t);
int main(int argc, char **argv) {
    if (argc != 2 || vpp_native_argc() != 2) return 10;
    int32_t arg = vpp_native_argv(1);
    if (arg <= 0 || vpp_native_bytes_length(arg) != (int32_t)strlen(argv[1])) return 11;
    for (int i = 0; argv[1][i]; ++i) {
        if (vpp_native_bytes_get(arg, i) != (unsigned char)argv[1][i]) return 12;
    }
    if (vpp_native_bytes_free(arg) != 0) return 13;
    int32_t key = vpp_native_bytes_new(), data = vpp_native_bytes_new();
    if (key <= 0 || data <= 0) return 14;
    if (vpp_native_bytes_append(key, 75) != 0) return 15;
    if (strcmp(argv[1], "write") == 0) {
        if (vpp_native_bytes_append(data, 52) != 0) return 16;
        if (vpp_native_cache_write(key, data) != 0) return 17;
    } else if (strcmp(argv[1], "read") == 0) {
        if (vpp_native_cache_read(key, data) != 1) return 18;
        if (vpp_native_bytes_length(data) != 1 || vpp_native_bytes_get(data, 0) != 52) return 19;
    } else if (strcmp(argv[1], "corrupt") == 0) {
        if (vpp_native_cache_read(key, data) != 0) return 20;
    } else return 21;
    if (vpp_native_bytes_free(key) != 0 || vpp_native_bytes_free(data) != 0) return 22;
    return 0;
}
]=])
    execute_process(COMMAND "${VPP_ELF_CC}" "${cache_source}"
        "${VPP_ROOT}/src/runtime/native/vpp_aot_runtime.c" -lcrypto
        -o "${cache_exe}"
        RESULT_VARIABLE result ERROR_VARIABLE errors TIMEOUT 45)
    if (NOT result EQUAL 0)
        message(FATAL_ERROR "Linux runtime cache smoke link failed: ${result}\n${errors}")
    endif()
    file(REMOVE_RECURSE "${cache_dir}")
    file(MAKE_DIRECTORY "${cache_dir}")
    foreach(mode IN ITEMS write read)
        execute_process(COMMAND "${CMAKE_COMMAND}" -E env
            "VPP_NATIVE_CACHE_DIR=${cache_dir}" "${cache_exe}" "${mode}"
            RESULT_VARIABLE result TIMEOUT 30)
        if (NOT result EQUAL 0)
            message(FATAL_ERROR "Linux native cache ${mode} failed: ${result}")
        endif()
    endforeach()
    file(GLOB cache_records "${cache_dir}/*.cache")
    list(LENGTH cache_records cache_count)
    if (NOT cache_count EQUAL 1)
        message(FATAL_ERROR "Linux native cache should publish one artifact, got ${cache_count}")
    endif()
    list(GET cache_records 0 cache_record)
    file(APPEND "${cache_record}" "tampered")
    execute_process(COMMAND "${CMAKE_COMMAND}" -E env
        "VPP_NATIVE_CACHE_DIR=${cache_dir}" "${cache_exe}" corrupt
        RESULT_VARIABLE result TIMEOUT 30)
    if (NOT result EQUAL 0)
        message(FATAL_ERROR "Linux native cache accepted damaged artifact: ${result}")
    endif()
endif()
message(STATUS "V++ x86-64 ELF64 native writer: PASS")
