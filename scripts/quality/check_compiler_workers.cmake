# Compile the same multi-module graph with serial and native V++ workers.
# Both paths must produce the same artifact and execute identically.
foreach(required IN ITEMS VPP_VM VPP_ROOT VPP_NATIVE_COMPILER VPP_OUTPUT_DIR)
    if (NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "Compiler workers: missing ${required}")
    endif()
endforeach()
if (NOT EXISTS "${VPP_NATIVE_COMPILER}")
    message(FATAL_ERROR "Compiler workers: native compiler missing: ${VPP_NATIVE_COMPILER}")
endif()
if (NOT EXISTS "${VPP_VM}")
    message(FATAL_ERROR "Compiler workers: standalone VM missing: ${VPP_VM}")
endif()

function(check_worker_profile profile workers expected_submitted expected_peak)
    if (NOT EXISTS "${profile}")
        message(FATAL_ERROR "Compiler workers=${workers}: missing profile")
    endif()
    file(READ "${profile}" trace)
    if (workers LESS 2)
        if (trace MATCHES "link\\.worker_(submitted|inflight_peak)")
            message(FATAL_ERROR "Serial compiler workers=${workers} submitted workers")
        endif()
    else()
        if (NOT trace MATCHES "link\\.worker_submitted ${expected_submitted}[\r\n]" OR
            NOT trace MATCHES "link\\.worker_inflight_peak ${expected_peak}[\r\n]")
            message(FATAL_ERROR "Compiler workers=${workers}: unexpected worker submission/peak:\n${trace}")
        endif()
    endif()
endfunction()

file(MAKE_DIRECTORY "${VPP_OUTPUT_DIR}/lib")
# A graph large enough for several grouped workers and a partial final group.
# Ten independent dependencies plus root, feature and math exercise 13 modules.
set(worker_imports "")
set(worker_sum "0")
foreach(i RANGE 0 9)
    file(WRITE "${VPP_OUTPUT_DIR}/lib/worker${i}.vi"
        "hàm giá trị() { trả về ${i}; };\n")
    string(APPEND worker_imports "nhập lib.worker${i}.* như w${i}; ")
    string(APPEND worker_sum " + w${i}.giá trị()")
endforeach()
file(WRITE "${VPP_OUTPUT_DIR}/main.vi"
    "nhập lib.feature.* như f; nhập lib.math.* như m; ${worker_imports} hàm chính() { in f.tính(); in m.cộng(2, 3); in ${worker_sum}; };\n")
file(WRITE "${VPP_OUTPUT_DIR}/lib/feature.vi"
    "nhập math như m; hàm tính() { trả về m.cộng(19, 23); };\n")
file(WRITE "${VPP_OUTPUT_DIR}/lib/math.vi"
    "hàm cộng(a, b) { trả về a + b; };\n")

# The compiler itself must run inside the standalone VM, rather than just
# using the VM to execute the artifact emitted by the native compiler.
set(compiler_image "${VPP_OUTPUT_DIR}/compiler.vmb")
file(REMOVE "${compiler_image}")
execute_process(
    COMMAND "${CMAKE_COMMAND}" -E env
        "VPP_HOME=${VPP_ROOT}" "VPP_AOT_ROOT=${VPP_ROOT}"
        "VPP_COMPILER_WORKERS=0" "VPP_COMPILER_PROFILE_FILE=" "VPP_NATIVE_NO_CACHE=1"
        "${VPP_NATIVE_COMPILER}" vm-build
        "${VPP_ROOT}/gói/biên dịch/dòng lệnh VM.vi" -o "${compiler_image}"
    RESULT_VARIABLE status ERROR_VARIABLE errors OUTPUT_VARIABLE output TIMEOUT 600)
if (NOT status STREQUAL "0" OR NOT EXISTS "${compiler_image}")
    message(FATAL_ERROR "Build VM compiler image failed (${status}): ${errors}\n${output}")
endif()

file(WRITE "${VPP_OUTPUT_DIR}/one-import.vi"
    "nhập lib.math.* như m; hàm chính() { in m.cộng(40, 2); };\n")
file(WRITE "${VPP_OUTPUT_DIR}/single-module.vi"
    "hàm chính() { in 42; };\n")

function(check_compilation backend graph workers submitted peak)
    set(out "${VPP_OUTPUT_DIR}/${backend}-${graph}-${workers}.vmb")
    set(profile "${VPP_OUTPUT_DIR}/${backend}-${graph}-${workers}.profile")
    file(REMOVE "${out}" "${profile}")
    execute_process(
        COMMAND "${CMAKE_COMMAND}" -E env
            "VPP_HOME=${VPP_ROOT}" "VPP_AOT_ROOT=${VPP_ROOT}"
            "VPP_COMPILER_WORKERS=${workers}" "VPP_NATIVE_NO_CACHE=1"
            "VPP_COMPILER_PROFILE_FILE=${profile}"
            ${ARGN} "${VPP_OUTPUT_DIR}/${graph}.vi" -o "${out}"
        RESULT_VARIABLE status ERROR_VARIABLE errors OUTPUT_VARIABLE output
        TIMEOUT 240)
    if (NOT status STREQUAL "0" OR NOT EXISTS "${out}")
        message(FATAL_ERROR "${backend} ${graph} workers=${workers} failed (${status}): ${errors}\n${output}")
    endif()
    check_worker_profile("${profile}" ${workers} ${submitted} ${peak})
    execute_process(COMMAND "${CMAKE_COMMAND}" -E compare_files
        "${VPP_OUTPUT_DIR}/native-${graph}-0.vmb" "${out}"
        RESULT_VARIABLE identical)
    if (NOT identical EQUAL 0)
        message(FATAL_ERROR "${backend} ${graph} workers=${workers}: bytecode or StringPool order changed")
    endif()
endfunction()

foreach(backend IN ITEMS native vm)
    if (backend STREQUAL "native")
        set(compiler "${VPP_NATIVE_COMPILER}" vm-build)
    else()
        set(compiler "${VPP_VM}" "${compiler_image}" build)
    endif()
    foreach(workers IN ITEMS 0 1 2 4 8)
        set(submitted 13)
        if (workers EQUAL 2)
            set(submitted 4)
        endif()
        check_compilation("${backend}" main ${workers} ${submitted} ${workers} ${compiler})
    endforeach()
    foreach(workers IN ITEMS 0 2)
        check_compilation("${backend}" one-import ${workers} 2 ${workers} ${compiler})
        check_compilation("${backend}" single-module ${workers} 1 1 ${compiler})
    endforeach()

    foreach(invalid IN ITEMS không_hợp_lệ -1 9 2.5 2abc)
        set(out "${VPP_OUTPUT_DIR}/${backend}-invalid.vmb")
        file(REMOVE "${out}")
        execute_process(
            COMMAND "${CMAKE_COMMAND}" -E env
                "VPP_HOME=${VPP_ROOT}" "VPP_AOT_ROOT=${VPP_ROOT}"
                "VPP_COMPILER_WORKERS=${invalid}" "VPP_COMPILER_PROFILE_FILE=" "VPP_NATIVE_NO_CACHE=1"
                ${compiler} "${VPP_OUTPUT_DIR}/main.vi" -o "${out}"
            RESULT_VARIABLE invalid_status ERROR_VARIABLE invalid_error
            OUTPUT_VARIABLE invalid_output TIMEOUT 120)
        if (NOT invalid_status MATCHES "^[1-9][0-9]*$" OR EXISTS "${out}" OR
            NOT invalid_error MATCHES "VPP_COMPILER_WORKERS")
            message(FATAL_ERROR "${backend} compiler ignored ${invalid}: ${invalid_status} ${invalid_error}\n${invalid_output}")
        endif()
    endforeach()
endforeach()

function(check_artifact artifact expected)
    execute_process(
        COMMAND "${CMAKE_COMMAND}" -E env "VPP_HOME=${VPP_ROOT}"
            "${VPP_VM}" "${artifact}"
        RESULT_VARIABLE status OUTPUT_VARIABLE output ERROR_VARIABLE errors TIMEOUT 120)
    if (NOT status STREQUAL "0" OR NOT output STREQUAL "${expected}")
        message(FATAL_ERROR "Worker artifact ${artifact} failed (${status}): ${errors}\n${output}")
    endif()
endfunction()
check_artifact("${VPP_OUTPUT_DIR}/vm-main-8.vmb" "42\n5\n45\n")
check_artifact("${VPP_OUTPUT_DIR}/vm-one-import-2.vmb" "42\n")
check_artifact("${VPP_OUTPUT_DIR}/vm-single-module-2.vmb" "42\n")

set(worker_fixture "${VPP_OUTPUT_DIR}/native-worker.vmb")
file(REMOVE "${worker_fixture}")
execute_process(
    COMMAND "${CMAKE_COMMAND}" -E env
        "VPP_HOME=${VPP_ROOT}" "VPP_AOT_ROOT=${VPP_ROOT}"
        "VPP_COMPILER_WORKERS=0" "VPP_COMPILER_PROFILE_FILE=" "VPP_NATIVE_NO_CACHE=1"
        "${VPP_NATIVE_COMPILER}" vm-build
        "${VPP_ROOT}/scripts/test/kiem_tra_native_worker.vi" -o "${worker_fixture}"
    RESULT_VARIABLE status ERROR_VARIABLE errors OUTPUT_VARIABLE output TIMEOUT 240)
if (NOT status STREQUAL "0" OR NOT EXISTS "${worker_fixture}")
    message(FATAL_ERROR "Build standalone VM worker fixture failed (${status}): ${errors}\n${output}")
endif()
check_artifact("${worker_fixture}" "native worker: đạt\n")

# The native executable must also be deterministic, including the emitted
# machine code, linked runtime metadata and platform executable headers.
set(exe_suffix "")
if (CMAKE_HOST_WIN32)
    set(exe_suffix ".exe")
endif()
foreach(workers IN ITEMS 0 1 2 4 8)
    set(out "${VPP_OUTPUT_DIR}/aot-${workers}${exe_suffix}")
    set(profile "${VPP_OUTPUT_DIR}/aot-${workers}.profile")
    file(REMOVE "${out}" "${profile}")
    execute_process(
        COMMAND "${CMAKE_COMMAND}" -E env
            "VPP_HOME=${VPP_ROOT}" "VPP_AOT_ROOT=${VPP_ROOT}"
            "VPP_COMPILER_WORKERS=${workers}" "VPP_NATIVE_NO_CACHE=1"
            "VPP_COMPILER_PROFILE_FILE=${profile}"
            "${VPP_NATIVE_COMPILER}" build "${VPP_OUTPUT_DIR}/main.vi" -o "${out}"
        RESULT_VARIABLE status ERROR_VARIABLE errors OUTPUT_VARIABLE output TIMEOUT 600)
    if (NOT status STREQUAL "0" OR NOT EXISTS "${out}")
        message(FATAL_ERROR "AOT workers=${workers} failed (${status}): ${errors}\n${output}")
    endif()
    set(submitted 13)
    if (workers EQUAL 2)
        set(submitted 4)
    endif()
    check_worker_profile("${profile}" ${workers} ${submitted} ${workers})
    execute_process(COMMAND "${CMAKE_COMMAND}" -E compare_files
        "${VPP_OUTPUT_DIR}/aot-0${exe_suffix}" "${out}" RESULT_VARIABLE identical)
    if (NOT identical EQUAL 0)
        message(FATAL_ERROR "AOT workers=${workers}: executable bytes changed")
    endif()
    execute_process(COMMAND "${out}" RESULT_VARIABLE status OUTPUT_VARIABLE output
        ERROR_VARIABLE errors TIMEOUT 120)
    if (NOT status STREQUAL "0" OR NOT output STREQUAL "42\n5\n45\n")
        message(FATAL_ERROR "AOT workers=${workers}: execution failed (${status}): ${errors}\n${output}")
    endif()
endforeach()
set(aot_fixture "${VPP_OUTPUT_DIR}/native-worker${exe_suffix}")
file(REMOVE "${aot_fixture}")
execute_process(
    COMMAND "${CMAKE_COMMAND}" -E env
        "VPP_HOME=${VPP_ROOT}" "VPP_AOT_ROOT=${VPP_ROOT}"
        "VPP_COMPILER_WORKERS=0" "VPP_NATIVE_NO_CACHE=1" "VPP_COMPILER_PROFILE_FILE="
        "${VPP_NATIVE_COMPILER}" build
        "${VPP_ROOT}/scripts/test/kiem_tra_native_worker.vi" -o "${aot_fixture}"
    RESULT_VARIABLE status OUTPUT_VARIABLE output ERROR_VARIABLE errors TIMEOUT 600)
if(NOT status STREQUAL "0" OR NOT EXISTS "${aot_fixture}")
    message(FATAL_ERROR "Build native AOT worker fixture failed (${status}): ${errors}\n${output}")
endif()
execute_process(COMMAND "${aot_fixture}" RESULT_VARIABLE status
    OUTPUT_VARIABLE output ERROR_VARIABLE errors TIMEOUT 120)
if(NOT status STREQUAL "0" OR NOT output STREQUAL "native worker: đạt\n")
    message(FATAL_ERROR "Native AOT worker fixture failed (${status}): ${errors}\n${output}")
endif()
message(STATUS "Compiler workers native/VM bytecode and AOT executable parity (0/1/2/4/8), invalid config and worker fixtures PASS")
