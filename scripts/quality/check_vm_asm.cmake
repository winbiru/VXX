foreach(required IN ITEMS VPP_STAGE1 VPP_NATIVE_COMPILER VPP_ROOT VPP_OUTPUT_DIR)
    if (NOT DEFINED ${required})
        message(FATAL_ERROR "VM ASM test missing ${required}")
    endif()
endforeach()
file(MAKE_DIRECTORY "${VPP_OUTPUT_DIR}")
set(ENV{VPP_HOME} "${VPP_ROOT}")
set(vm "${VPP_OUTPUT_DIR}/vpp-vm")
execute_process(COMMAND "${VPP_STAGE1}" build "${VPP_ROOT}/gói/máy ảo/chính.vi" -o "${vm}"
    RESULT_VARIABLE status OUTPUT_VARIABLE output ERROR_VARIABLE errors TIMEOUT 300)
if (NOT status STREQUAL "0")
    message(FATAL_ERROR "VM ASM compilation failed (${status}):\n${output}\n${errors}")
endif()
set(image "${VPP_OUTPUT_DIR}/runtime.vmb")
execute_process(COMMAND "${VPP_NATIVE_COMPILER}" vm-build
    "${VPP_ROOT}/src/tests/fixtures/vm_asm.vi" -o "${image}"
    RESULT_VARIABLE status OUTPUT_VARIABLE output ERROR_VARIABLE errors TIMEOUT 120)
if (NOT status STREQUAL "0")
    message(FATAL_ERROR "VM image backend failed (${status}):\n${output}\n${errors}")
endif()
# No external compiler/linker can be reached while the standalone VM runs.
execute_process(COMMAND "${CMAKE_COMMAND}" -E env "PATH=" "${vm}" "${image}"
    WORKING_DIRECTORY "${VPP_OUTPUT_DIR}"
    RESULT_VARIABLE status OUTPUT_VARIABLE output ERROR_VARIABLE errors TIMEOUT 120)
set(expected "-2147483648\n-2147483648\n-1\n3.75\n3.0\ni\n[1, 9, 3]\n16\n41\n42\n9\n25\nswitch\nngoại lệ VM\nASM VM OK\n")
if (NOT status STREQUAL "0" OR NOT output STREQUAL expected)
    message(FATAL_ERROR "VM runtime mismatch (${status}):\n${output}\n${errors}")
endif()
if (APPLE)
    execute_process(COMMAND /usr/bin/nm -u "${vm}" OUTPUT_VARIABLE symbols RESULT_VARIABLE status)
elseif (UNIX)
    execute_process(COMMAND nm -u "${vm}" OUTPUT_VARIABLE symbols RESULT_VARIABLE status)
endif()
if (NOT status STREQUAL "0" OR symbols MATCHES "__cxa_throw|__cxa_allocate_exception|_ZSt|_ZNSt|vpp_aot_|executeVpp|linkedBytecodeRun")
    message(FATAL_ERROR "VM depends on C++ runtime/compiler symbols:\n${symbols}")
endif()
file(WRITE "${VPP_OUTPUT_DIR}/invalid.vmb" "invalid")
execute_process(COMMAND "${vm}" "${VPP_OUTPUT_DIR}/invalid.vmb"
    RESULT_VARIABLE status OUTPUT_VARIABLE output ERROR_VARIABLE errors TIMEOUT 10)
if (status STREQUAL "0" OR NOT errors MATCHES "error=10")
    message(FATAL_ERROR "VM accepted malformed image: ${status}\n${output}\n${errors}")
endif()
file(WRITE "${VPP_OUTPUT_DIR}/gc.vi" "hàm chính() { giữ = [9]; longtext = \"x\"; lặp(j = 0; j < 16; j++) { longtext = longtext + longtext; }; i = 0; lặp(i = 0; i < 2000; i++) { rác = [i]; thêm(rác, rác); temp = longtext + \"\"; }; in giữ[0]; in i; };")
execute_process(COMMAND "${VPP_NATIVE_COMPILER}" vm-build "${VPP_OUTPUT_DIR}/gc.vi" -o "${VPP_OUTPUT_DIR}/gc.vmb"
    RESULT_VARIABLE status OUTPUT_VARIABLE output ERROR_VARIABLE errors TIMEOUT 120)
if (NOT status STREQUAL "0")
    message(FATAL_ERROR "GC fixture compilation failed: ${status}\n${output}\n${errors}")
endif()
execute_process(COMMAND "${vm}" "${VPP_OUTPUT_DIR}/gc.vmb"
    RESULT_VARIABLE status OUTPUT_VARIABLE output ERROR_VARIABLE errors TIMEOUT 180)
if (NOT status STREQUAL "0" OR NOT output STREQUAL "9\n2000\n")
    message(FATAL_ERROR "GC failed to preserve roots/reclaim cycles: ${status}\n${output}\n${errors}")
endif()
# Resolve each static host name once. Exercise both direct host calls and
# aliases, so a stale/wrong StringPool cache entry changes observable output.
file(WRITE "${VPP_OUTPUT_DIR}/host-cache.vi" [=[hàm chính() {
    ds = [1];
    máy ảo thêm cuối danh sách(ds, 2);
    thêm(ds, 3);
    in máy ảo lấy độ dài(ds);
    in độ dài(ds);
    in do_dai(ds);
    in máy ảo lấy loại(ds);
    in ds;
    in máy ảo lấy độ dài("á🐱");
};
]=])
execute_process(COMMAND "${VPP_NATIVE_COMPILER}" vm-build
    "${VPP_OUTPUT_DIR}/host-cache.vi" -o "${VPP_OUTPUT_DIR}/host-cache.vmb"
    RESULT_VARIABLE status OUTPUT_VARIABLE output ERROR_VARIABLE errors TIMEOUT 120)
if (NOT status STREQUAL "0")
    message(FATAL_ERROR "Host cache fixture compilation failed: ${status}\n${output}\n${errors}")
endif()
execute_process(COMMAND "${vm}" "${VPP_OUTPUT_DIR}/host-cache.vmb"
    RESULT_VARIABLE status OUTPUT_VARIABLE output ERROR_VARIABLE errors TIMEOUT 30)
if (NOT status STREQUAL "0" OR NOT output STREQUAL "3\n3\n3\ndanh sách\n[1, 2, 3]\n2\n")
    message(FATAL_ERROR "Host cache/direct call regression: ${status}\n${output}\n${errors}")
endif()
file(WRITE "${VPP_OUTPUT_DIR}/host-cache-invalid.vi"
    "hàm chính() { thêm(12, 34); };\n")
execute_process(COMMAND "${VPP_NATIVE_COMPILER}" vm-build
    "${VPP_OUTPUT_DIR}/host-cache-invalid.vi" -o "${VPP_OUTPUT_DIR}/host-cache-invalid.vmb"
    RESULT_VARIABLE status OUTPUT_VARIABLE output ERROR_VARIABLE errors TIMEOUT 120)
if (NOT status STREQUAL "0")
    message(FATAL_ERROR "Host fast-path type fixture compilation failed: ${status}\n${output}\n${errors}")
endif()
execute_process(COMMAND "${vm}" "${VPP_OUTPUT_DIR}/host-cache-invalid.vmb"
    RESULT_VARIABLE status OUTPUT_VARIABLE output ERROR_VARIABLE errors TIMEOUT 30)
if (status STREQUAL "0" OR NOT errors MATCHES "error=2")
    message(FATAL_ERROR "Host fast-path accepted non-list append: ${status}\n${output}\n${errors}")
endif()
# A long live reference chain must survive multiple collections. This also
# catches quadratic full-heap tracing when references have large depth.
file(WRITE "${VPP_OUTPUT_DIR}/gc-deep.vi" [=[hàm chính() {
    head = [123];
    lặp(i = 0; i < 12000; i++) { head = [head]; };
    lặp(i = 0; i < 12000; i++) { head = head[0]; };
    in head[0];
};
]=])
execute_process(COMMAND "${VPP_NATIVE_COMPILER}" vm-build "${VPP_OUTPUT_DIR}/gc-deep.vi" -o "${VPP_OUTPUT_DIR}/gc-deep.vmb"
    RESULT_VARIABLE status OUTPUT_VARIABLE output ERROR_VARIABLE errors TIMEOUT 120)
if (NOT status STREQUAL "0")
    message(FATAL_ERROR "Deep GC fixture compilation failed: ${status}\n${output}\n${errors}")
endif()
execute_process(COMMAND "${vm}" "${VPP_OUTPUT_DIR}/gc-deep.vmb"
    RESULT_VARIABLE status OUTPUT_VARIABLE output ERROR_VARIABLE errors TIMEOUT 120)
if (NOT status STREQUAL "0" OR NOT output STREQUAL "123\n")
    message(FATAL_ERROR "Deep GC graph regression: ${status}\n${output}\n${errors}")
endif()
# Source files stress Unicode codepoint indexing and repeated string lengths.
# Check boundaries on both sides of the 64-codepoint sparse-index stride.
file(WRITE "${VPP_OUTPUT_DIR}/utf8-index.vi" [=[hàm chính() {
    s = "";
    lặp(i = 0; i < 150; i++) { s = s + "á🐱"; };
    in độ dài(s);
    in s[0]; in s[1]; in s[63]; in s[64]; in s[128]; in s[299];
    t = "";
    lặp(i = 0; i < 140; i++) { t = t + "A€"; };
    in độ dài(t);
    in t[0]; in t[1]; in t[127]; in t[128]; in t[279];
    ascii = "";
    lặp(i = 0; i < 200; i++) { ascii = ascii + "q"; };
    in ascii[0]; in ascii[63]; in ascii[64]; in ascii[199];
    lặp(i = 0; i < 5000; i++) { rác = [i, i]; };
    in độ dài(s); in s[299];
    in độ dài(t); in t[128]; in t[279];
    in ascii[199]; in ascii[64] == "q";
    in độ dài(""); in độ dài("a");
};
]=])
execute_process(COMMAND "${VPP_NATIVE_COMPILER}" vm-build
    "${VPP_OUTPUT_DIR}/utf8-index.vi" -o "${VPP_OUTPUT_DIR}/utf8-index.vmb"
    RESULT_VARIABLE status OUTPUT_VARIABLE output ERROR_VARIABLE errors TIMEOUT 120)
if (NOT status STREQUAL "0")
    message(FATAL_ERROR "UTF-8 index fixture compilation failed: ${status}\n${output}\n${errors}")
endif()
execute_process(COMMAND "${vm}" "${VPP_OUTPUT_DIR}/utf8-index.vmb"
    RESULT_VARIABLE status OUTPUT_VARIABLE output ERROR_VARIABLE errors TIMEOUT 120)
if (NOT status STREQUAL "0" OR NOT output STREQUAL "300\ná\n🐱\n🐱\ná\ná\n🐱\n280\nA\n€\n€\nA\n€\nq\nq\nq\nq\n300\n🐱\n280\nA\n€\nq\n1\n0\n1\n")
    message(FATAL_ERROR "UTF-8 sparse index regression: ${status}\n${output}\n${errors}")
endif()
file(WRITE "${VPP_OUTPUT_DIR}/utf8-invalid.vi" [=[hàm chính() {
    s = "";
    lặp(i = 0; i < 150; i++) { s = s + "á🐱"; };
    in s[300];
};
]=])
execute_process(COMMAND "${VPP_NATIVE_COMPILER}" vm-build
    "${VPP_OUTPUT_DIR}/utf8-invalid.vi" -o "${VPP_OUTPUT_DIR}/utf8-invalid.vmb"
    RESULT_VARIABLE status OUTPUT_VARIABLE output ERROR_VARIABLE errors TIMEOUT 120)
if (NOT status STREQUAL "0")
    message(FATAL_ERROR "UTF-8 bounds fixture compilation failed: ${status}\n${output}\n${errors}")
endif()
execute_process(COMMAND "${vm}" "${VPP_OUTPUT_DIR}/utf8-invalid.vmb"
    RESULT_VARIABLE status OUTPUT_VARIABLE output ERROR_VARIABLE errors TIMEOUT 30)
if (status STREQUAL "0" OR NOT errors MATCHES "error=4")
    message(FATAL_ERROR "VM accepted out-of-bounds UTF-8 index: ${status}\n${output}\n${errors}")
endif()
execute_process(COMMAND "${VPP_NATIVE_COMPILER}" vm-build
    "${VPP_ROOT}/src/tests/fixtures/vm_asm_host.vi" -o "${VPP_OUTPUT_DIR}/host.vmb"
    RESULT_VARIABLE status OUTPUT_VARIABLE output ERROR_VARIABLE errors TIMEOUT 120)
if (NOT status STREQUAL "0")
    message(FATAL_ERROR "Host fixture compilation failed: ${status}\n${output}\n${errors}")
endif()
execute_process(COMMAND "${CMAKE_COMMAND}" -E env "PATH=" "VPP_VM_ASM_TEST=host-direct"
    "${vm}" "${VPP_OUTPUT_DIR}/host.vmb"
    WORKING_DIRECTORY "${VPP_OUTPUT_DIR}"
    RESULT_VARIABLE status OUTPUT_VARIABLE output ERROR_VARIABLE errors TIMEOUT 180)
if (NOT status STREQUAL "0" OR NOT output STREQUAL "[0, 255, 10, 52]\n1\nhost-direct\nHOST ASM OK\n")
    message(FATAL_ERROR "Direct ASM host ABI failed: ${status}\n${output}\n${errors}")
endif()
file(WRITE "${VPP_OUTPUT_DIR}/map-index.vi" [=[hàm chính() {
    m = {"z": 9, "a": 1, "c": 3, "b": 2};
    in m["a"];
    in m["z"];
    in máy ảo băm định danh(m) == máy ảo băm định danh(m);
};
]=])
execute_process(COMMAND "${VPP_NATIVE_COMPILER}" vm-build
    "${VPP_OUTPUT_DIR}/map-index.vi" -o "${VPP_OUTPUT_DIR}/map-index.vmb"
    RESULT_VARIABLE status OUTPUT_VARIABLE output ERROR_VARIABLE errors TIMEOUT 120)
if (NOT status STREQUAL "0")
    message(FATAL_ERROR "VM map fixture compilation failed: ${status}\n${output}\n${errors}")
endif()
execute_process(COMMAND "${vm}" "${VPP_OUTPUT_DIR}/map-index.vmb"
    RESULT_VARIABLE status OUTPUT_VARIABLE output ERROR_VARIABLE errors TIMEOUT 30)
if (NOT status STREQUAL "0" OR NOT output STREQUAL "1\n9\n1\n")
    message(FATAL_ERROR "VM map/index/hash mismatch: ${status}\n${output}\n${errors}")
endif()
file(WRITE "${VPP_OUTPUT_DIR}/identity-invalid.vi"
    "hàm chính() { in máy ảo băm định danh(3); };\n")
execute_process(COMMAND "${VPP_NATIVE_COMPILER}" vm-build
    "${VPP_OUTPUT_DIR}/identity-invalid.vi" -o "${VPP_OUTPUT_DIR}/identity-invalid.vmb"
    RESULT_VARIABLE status OUTPUT_VARIABLE output ERROR_VARIABLE errors TIMEOUT 120)
if (NOT status STREQUAL "0")
    message(FATAL_ERROR "VM invalid identity fixture compilation failed: ${status}\n${output}\n${errors}")
endif()
execute_process(COMMAND "${vm}" "${VPP_OUTPUT_DIR}/identity-invalid.vmb"
    RESULT_VARIABLE status OUTPUT_VARIABLE output ERROR_VARIABLE errors TIMEOUT 30)
if (status STREQUAL "0" OR NOT errors MATCHES "error=2")
    message(FATAL_ERROR "VM accepted identity hash on scalar: ${status}\n${output}\n${errors}")
endif()
set(VPP_VM "${vm}")
include("${CMAKE_CURRENT_LIST_DIR}/check_vm_asm_system.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/check_vm_asm_directory.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/check_vm_asm_process.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/check_vm_asm_resolver.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/check_vm_asm_socket.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/check_vm_tls_crypto.cmake")
message(STATUS "V++/ASM VM: values, control flow, closures, OOP, exceptions, GC and no C++ symbols PASS")
