# CLI regression checks driven by CMake with .vi fixtures.
if (NOT DEFINED VPP_EXEC OR NOT DEFINED VPP_ROOT OR NOT DEFINED VPP_CASE OR
    NOT DEFINED VPP_OUTPUT_DIR)
    message(FATAL_ERROR "V++ CLI check: missing arguments")
endif()
file(MAKE_DIRECTORY "${VPP_OUTPUT_DIR}")
set(VPP_CHECK_TIMEOUT 240)
if (DEFINED VPP_NATIVE_COMPILER AND NOT "${VPP_NATIVE_COMPILER}" STREQUAL "")
    if (NOT EXISTS "${VPP_NATIVE_COMPILER}")
        message(FATAL_ERROR "Native V++ compiler missing: ${VPP_NATIVE_COMPILER}")
    endif()
    set(ENV{VPP_NATIVE_COMPILER} "${VPP_NATIVE_COMPILER}")
endif()

function(vpp_check expected)
    execute_process(COMMAND "${CMAKE_COMMAND}" -E env "VPP_HOME=${VPP_ROOT}"
        "${VPP_EXEC}" ${ARGN}
        WORKING_DIRECTORY "${VPP_OUTPUT_DIR}"
        RESULT_VARIABLE status OUTPUT_VARIABLE output ERROR_VARIABLE errors TIMEOUT ${VPP_CHECK_TIMEOUT})
    # Windows CRT stdout uses CRLF, while the fixtures use LF. Compare the
    # logical output without hiding extra lines or changes to its contents.
    string(REPLACE "\r\n" "\n" output "${output}")
    if (NOT status EQUAL 0 OR NOT output STREQUAL expected)
        # Render invisible line endings as hex to diagnose Windows failures
        # where the two multiline strings appear identical in Actions logs.
        string(LENGTH "${output}" output_length)
        string(LENGTH "${expected}" expected_length)
        string(HEX "${output}" output_hex)
        string(HEX "${expected}" expected_hex)
        string(SUBSTRING "${output_hex}" 0 256 output_hex)
        string(SUBSTRING "${expected_hex}" 0 256 expected_hex)
        message(FATAL_ERROR "V++ CLI failed (${status}): ${ARGN}\n${errors}\nOutput: ${output}\nExpected: ${expected}\nOutput length: ${output_length}, hex prefix: ${output_hex}\nExpected length: ${expected_length}, hex prefix: ${expected_hex}")
    endif()
endfunction()

if (VPP_CASE STREQUAL "compiler")
    set(fixtures "${VPP_ROOT}/src/tests/fixtures/checkcompilervpp")
    file(MAKE_DIRECTORY "${VPP_OUTPUT_DIR}/lib")
    file(COPY "${fixtures}/lib/math.vi" DESTINATION "${VPP_OUTPUT_DIR}/lib")
    file(COPY "${fixtures}/main.vi" DESTINATION "${VPP_OUTPUT_DIR}")
    set(main "${VPP_OUTPUT_DIR}/main.vi")
    set(expected "init\n42\n7\n7\n2\ntiếng Việt\nđúng\n")
    vpp_check("${expected}" run --compiler=cpp "${main}")
    vpp_check("${expected}" run --compiler=vpp "${main}")
    # Legacy VM programs resolve relative runtime I/O from the entry file's
    # directory. The native compiler runs in a subprocess and must retain the
    # same behavior even when invoked from a different working directory.
    set(relative_dir "${VPP_OUTPUT_DIR}/relative-runtime")
    file(MAKE_DIRECTORY "${relative_dir}")
    file(WRITE "${relative_dir}/relative.txt" "relative-ok")
    file(WRITE "${relative_dir}/main.vi"
        "nhập gói.nhập xuất.tệp.*; in đọc tệp(\"relative.txt\");\n")
    file(WRITE "${VPP_OUTPUT_DIR}/relative.txt" "wrong-directory")
    set(relative_main "${relative_dir}/main.vi")
    vpp_check("relative-ok\n" run --compiler=cpp "${relative_main}")
    vpp_check("relative-ok\n" run --compiler=vpp "${relative_main}")
    vpp_check("relative-ok\n" run "${relative_main}")
    # The batch runner's .output golden includes compiler diagnostics even
    # though a native compiler emits them from a separate process on stderr.
    set(invalid_batch "${VPP_OUTPUT_DIR}/batch-invalid.vi")
    file(WRITE "${invalid_batch}" "hàm x( {\n")
    execute_process(COMMAND "${CMAKE_COMMAND}" -E env "VPP_HOME=${VPP_ROOT}"
        "${VPP_EXEC}" --batch-run-tests "${VPP_OUTPUT_DIR}/batch-invalid-output"
        "${invalid_batch}"
        WORKING_DIRECTORY "${VPP_OUTPUT_DIR}"
        RESULT_VARIABLE batch_status ERROR_VARIABLE batch_errors TIMEOUT ${VPP_CHECK_TIMEOUT})
    file(READ "${VPP_OUTPUT_DIR}/batch-invalid-output/batch-invalid.status" batch_case_status)
    file(READ "${VPP_OUTPUT_DIR}/batch-invalid-output/batch-invalid.output" batch_case_output)
    if (NOT batch_status EQUAL 0 OR batch_case_status STREQUAL "0\n" OR
        NOT batch_case_output MATCHES "ngoại lệ")
        message(FATAL_ERROR "Native batch runner lost stderr or failure status: ${batch_status} ${batch_case_status} ${batch_case_output} ${batch_errors}")
    endif()
    # A source checkout with a bootstrapped compiler should work without
    # exported toolchain environment variables. Artifact output also proves
    # the build-tree native compiler was discovered (Stage 0 has no -o path).
    if (UNIX)
        set(rootless_artifact "${VPP_OUTPUT_DIR}/rootless.vbc")
        execute_process(COMMAND "${CMAKE_COMMAND}" -E env
            --unset=VPP_HOME --unset=VPP_AOT_ROOT --unset=VPP_NATIVE_COMPILER
            --unset=VPP_AOT_LIB_ROOT "${VPP_EXEC}" build "${relative_main}"
            -o "${rootless_artifact}"
            WORKING_DIRECTORY "${VPP_OUTPUT_DIR}"
            RESULT_VARIABLE rootless_status ERROR_VARIABLE rootless_error
            TIMEOUT ${VPP_CHECK_TIMEOUT})
        if (NOT rootless_status EQUAL 0 OR NOT EXISTS "${rootless_artifact}")
            message(FATAL_ERROR "Build-tree V++/ASM discovery failed without VPP_HOME: ${rootless_status} ${rootless_error}")
        endif()
    endif()
    # When Stage 1 is present, run and the single-file CLI must select the
    # V++/ASM path without requiring an explicit --compiler flag.
    vpp_check("${expected}" run "${main}")
    vpp_check("${expected}" "${main}")
    execute_process(COMMAND "${CMAKE_COMMAND}" -E env "VPP_HOME=${VPP_ROOT}"
        "${VPP_EXEC}" test "${main}"
        WORKING_DIRECTORY "${VPP_OUTPUT_DIR}"
        RESULT_VARIABLE test_status OUTPUT_VARIABLE test_output ERROR_VARIABLE test_error TIMEOUT 240)
    if (NOT test_status EQUAL 0 OR NOT test_output MATCHES "Kiểm thử hoàn tất: 1/1")
        message(FATAL_ERROR "Default V++ test failed (${test_status}): ${test_error}\n${test_output}")
    endif()
    if (UNIX)
        file(WRITE "${VPP_OUTPUT_DIR}/repl-input.txt" "in 42;\n:quit\n")
        execute_process(COMMAND "${CMAKE_COMMAND}" -E env "VPP_HOME=${VPP_ROOT}"
            "${VPP_EXEC}" --repl --compiler=vpp
            INPUT_FILE "${VPP_OUTPUT_DIR}/repl-input.txt"
            WORKING_DIRECTORY "${VPP_OUTPUT_DIR}"
            RESULT_VARIABLE repl_status OUTPUT_VARIABLE repl_output ERROR_VARIABLE repl_error TIMEOUT 240)
        if (NOT repl_status EQUAL 0 OR NOT repl_output MATCHES "vpp> 42")
            message(FATAL_ERROR "Native V++ REPL failed (${repl_status}): ${repl_error}\n${repl_output}")
        endif()
        file(WRITE "${VPP_OUTPUT_DIR}/repl-invalid-input.txt" "hàm x( {\n:quit\n")
        execute_process(COMMAND "${CMAKE_COMMAND}" -E env "VPP_HOME=${VPP_ROOT}"
            "${VPP_EXEC}" --repl --compiler=vpp
            INPUT_FILE "${VPP_OUTPUT_DIR}/repl-invalid-input.txt"
            WORKING_DIRECTORY "${VPP_OUTPUT_DIR}"
            RESULT_VARIABLE invalid_repl_status ERROR_VARIABLE invalid_repl_error TIMEOUT 240)
        if (invalid_repl_status EQUAL 0 OR NOT invalid_repl_error MATCHES "REPL: compiler V\\+\\+ thoát")
            message(FATAL_ERROR "Native REPL hid compiler failure: ${invalid_repl_status} ${invalid_repl_error}")
        endif()
    endif()
    execute_process(COMMAND "${CMAKE_COMMAND}" -E env "VPP_HOME=${VPP_ROOT}"
        "${VPP_EXEC}" build "${main}"
        WORKING_DIRECTORY "${VPP_OUTPUT_DIR}"
        RESULT_VARIABLE default_build_status ERROR_VARIABLE default_build_error TIMEOUT 240)
    if (NOT default_build_status EQUAL 0)
        message(FATAL_ERROR "Default V++ build failed: ${default_build_error}")
    endif()
    set(default_artifact "${VPP_OUTPUT_DIR}/default-app.vbc")
    execute_process(COMMAND "${CMAKE_COMMAND}" -E env "VPP_HOME=${VPP_ROOT}"
        "${VPP_EXEC}" build "${main}" -o "${default_artifact}"
        WORKING_DIRECTORY "${VPP_OUTPUT_DIR}"
        RESULT_VARIABLE default_artifact_status ERROR_VARIABLE default_artifact_error TIMEOUT 240)
    if (NOT default_artifact_status EQUAL 0 OR NOT EXISTS "${default_artifact}")
        message(FATAL_ERROR "Default V++ artifact build failed: ${default_artifact_error}")
    endif()
    vpp_check("${expected}" run --artifact "${default_artifact}")
    if (UNIX)
        # A shim makes dispatch observable: if the CLI accidentally falls back
        # to its C++/VM path, this exact marker can never be produced.
        set(native_probe "${VPP_OUTPUT_DIR}/native-default-probe.sh")
        file(WRITE "${native_probe}" "#!/bin/sh\nprintf 'native-selected\\n'\n")
        file(CHMOD "${native_probe}" PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE)
        set(saved_native_compiler "$ENV{VPP_NATIVE_COMPILER}")
        set(ENV{VPP_NATIVE_COMPILER} "${native_probe}")
        foreach(probe IN ITEMS run build direct disassemble)
            if (probe STREQUAL "direct")
                set(probe_args "${main}")
            elseif (probe STREQUAL "disassemble")
                set(probe_args "--giải-mã" "${main}")
            else()
                set(probe_args "${probe}" "${main}")
            endif()
            vpp_check("native-selected\n" ${probe_args})
        endforeach()
        execute_process(COMMAND "${CMAKE_COMMAND}" -E env "VPP_HOME=${VPP_ROOT}"
            "${VPP_EXEC}" test "${main}"
            RESULT_VARIABLE test_probe_status OUTPUT_VARIABLE test_probe_output TIMEOUT 240)
        if (NOT test_probe_status EQUAL 0 OR NOT test_probe_output MATCHES "native-selected")
            message(FATAL_ERROR "Test command did not dispatch to V++ compiler")
        endif()
        execute_process(COMMAND "${CMAKE_COMMAND}" -E env "VPP_HOME=${VPP_ROOT}"
            "${VPP_EXEC}" --batch-run-tests "${VPP_OUTPUT_DIR}/batch-probe" "${main}"
            RESULT_VARIABLE batch_probe_status TIMEOUT 240)
        file(READ "${VPP_OUTPUT_DIR}/batch-probe/main.output" batch_probe_output)
        if (NOT batch_probe_status EQUAL 0 OR NOT batch_probe_output STREQUAL "native-selected\n")
            message(FATAL_ERROR "Batch tests did not dispatch to V++ compiler")
        endif()
        execute_process(COMMAND "${CMAKE_COMMAND}" -E env "VPP_HOME=${VPP_ROOT}"
            "${VPP_EXEC}" --repl
            INPUT_FILE "${VPP_OUTPUT_DIR}/repl-input.txt"
            RESULT_VARIABLE repl_probe_status OUTPUT_VARIABLE repl_probe_output TIMEOUT 240)
        if (NOT repl_probe_status EQUAL 0 OR NOT repl_probe_output MATCHES "native-selected")
            message(FATAL_ERROR "REPL did not dispatch to V++ compiler")
        endif()
        set(ENV{VPP_NATIVE_COMPILER} "${saved_native_compiler}")
    endif()
    set(artifact "${VPP_OUTPUT_DIR}/app.vbc")
    execute_process(COMMAND "${CMAKE_COMMAND}" -E env "VPP_HOME=${VPP_ROOT}"
        "${VPP_EXEC}" build --compiler=vpp "${main}" -o "${artifact}"
        WORKING_DIRECTORY "${VPP_OUTPUT_DIR}"
        RESULT_VARIABLE status ERROR_VARIABLE errors TIMEOUT 240)
    if (NOT status EQUAL 0 OR NOT EXISTS "${artifact}")
        message(FATAL_ERROR "V++ artifact build failed: ${errors}")
    endif()
    file(REMOVE "${main}" "${VPP_OUTPUT_DIR}/lib/math.vi")
    vpp_check("${expected}" run --artifact "${artifact}")
    file(COPY "${fixtures}/closure.vi" DESTINATION "${VPP_OUTPUT_DIR}")
    set(closure_source "${VPP_OUTPUT_DIR}/closure.vi")
    set(closure_artifact "${VPP_OUTPUT_DIR}/closure.vbc")
    vpp_check("1\n" run --compiler=cpp "${closure_source}")
    vpp_check("1\n" run --compiler=vpp "${closure_source}")
    execute_process(COMMAND "${CMAKE_COMMAND}" -E env "VPP_HOME=${VPP_ROOT}"
        "${VPP_EXEC}" build --compiler=vpp "${closure_source}" -o "${closure_artifact}"
        RESULT_VARIABLE status TIMEOUT 240)
    if (NOT status EQUAL 0)
        message(FATAL_ERROR "Compiler V++ không tạo được artifact closure")
    endif()
    file(REMOVE "${closure_source}")
    vpp_check("1\n" run --artifact "${closure_artifact}")
    # Closure hợp lệ đã được hỗ trợ; dùng tham số trùng để thử giữ artifact cũ.
    file(WRITE "${VPP_OUTPUT_DIR}/invalid-closure.vi"
        "hàm chính() { biến f = hàm(x, x) { trả về x; }; };\n")
    execute_process(COMMAND "${CMAKE_COMMAND}" -E env "VPP_HOME=${VPP_ROOT}"
        "${VPP_EXEC}" build --compiler=vpp
        "${VPP_OUTPUT_DIR}/invalid-closure.vi" -o "${artifact}"
        RESULT_VARIABLE status TIMEOUT 240)
    if (status EQUAL 0)
        message(FATAL_ERROR "Compiler V++ chấp nhận closure trùng tham số")
    endif()
    vpp_check("${expected}" run --artifact "${artifact}")
    # Exercise boundary arithmetic through both source compilers, and then run
    # the V++-generated artifact after its source has been removed.
    file(COPY "${fixtures}/i32.vi" DESTINATION "${VPP_OUTPUT_DIR}")
    set(i32_source "${VPP_OUTPUT_DIR}/i32.vi")
    set(i32_expected "-2147483648\n2147483647\n1\n-2147483648\n2147483646\n2147483647\n2147483647\n-2147483647\n-2147483648\n-2147483648\n2147483647\n1\n-2147483648\n-2147483648\n-2147483648\n2147483647\n2147483647\n-2147483648\n")
    vpp_check("${i32_expected}" run --compiler=cpp "${i32_source}")
    vpp_check("${i32_expected}" run --compiler=vpp "${i32_source}")
    set(i32_artifact "${VPP_OUTPUT_DIR}/i32.vbc")
    execute_process(COMMAND "${CMAKE_COMMAND}" -E env "VPP_HOME=${VPP_ROOT}"
        "${VPP_EXEC}" build --compiler=vpp "${i32_source}" -o "${i32_artifact}"
        WORKING_DIRECTORY "${VPP_OUTPUT_DIR}"
        RESULT_VARIABLE status OUTPUT_VARIABLE output ERROR_VARIABLE errors TIMEOUT 240)
    if (NOT status EQUAL 0)
        message(FATAL_ERROR "V++ i32 artifact build failed (${status}):\n${errors}\n${output}")
    endif()
    file(REMOVE "${i32_source}")
    vpp_check("${i32_expected}" run --artifact "${i32_artifact}")
    # A compiled host invokes a second native compiler through the embedded
    # scripting ABI. This must work without entering the VM interpreter.
    file(MAKE_DIRECTORY "${VPP_OUTPUT_DIR}/embedded/lib")
    file(COPY "${fixtures}/lib/math.vi" DESTINATION "${VPP_OUTPUT_DIR}/embedded/lib")
    # Runtime-relative paths resolve beside the executable entry source, so
    # keep the embedded/ directory and its invoking fixture together.
    file(COPY "${fixtures}/embedded_native.vi" DESTINATION "${VPP_OUTPUT_DIR}")
    vpp_check("1\n1\n" run --compiler=vpp "${VPP_OUTPUT_DIR}/embedded_native.vi")
    if (UNIX AND DEFINED VPP_NATIVE_COMPILER AND NOT "${VPP_NATIVE_COMPILER}" STREQUAL "")
        # The script is outside its import base. A native source graph with
        # a virtual entrypoint must work even when the base cannot be written.
        set(embedded_source "${VPP_OUTPUT_DIR}/embedded-source.vi")
        file(WRITE "${embedded_source}" "nhập lib.math.* như m; in m.cộng(40);")
        file(CHMOD "${VPP_OUTPUT_DIR}/embedded" PERMISSIONS
            OWNER_READ OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE)
        execute_process(COMMAND "${CMAKE_COMMAND}" -E env "VPP_HOME=${VPP_ROOT}"
            "${VPP_NATIVE_COMPILER}" embed "${embedded_source}"
            --base "${VPP_OUTPUT_DIR}/embedded"
            WORKING_DIRECTORY "${VPP_OUTPUT_DIR}"
            RESULT_VARIABLE embedded_status OUTPUT_VARIABLE embedded_output
            ERROR_VARIABLE embedded_error TIMEOUT 240)
        file(CHMOD "${VPP_OUTPUT_DIR}/embedded" PERMISSIONS
            OWNER_READ OWNER_WRITE OWNER_EXECUTE)
        if (NOT embedded_status EQUAL 0 OR NOT embedded_output STREQUAL "init\n42\n")
            message(FATAL_ERROR "Native embedded import from read-only base failed (${embedded_status}):\n${embedded_error}\n${embedded_output}")
        endif()
    endif()
    message(STATUS "V++ compiler CLI and artifact preservation: PASS")
elseif (VPP_CASE STREQUAL "selfhost")
    set(VPP_CHECK_TIMEOUT 600)
    set(source "${VPP_ROOT}/src/tests/kiem_tra_selfhost_i32_artifact.vi")
    set(artifact "${VPP_OUTPUT_DIR}/selfhost-i32-artifact.vbc")
    set(expected "selfhost_i32_artifact_ok\n")
    vpp_check("${expected}" run --compiler=cpp "${source}")
    execute_process(COMMAND "${CMAKE_COMMAND}" -E env "VPP_HOME=${VPP_ROOT}"
        "${VPP_EXEC}" build --compiler=vpp "${source}" -o "${artifact}"
        WORKING_DIRECTORY "${VPP_OUTPUT_DIR}"
        RESULT_VARIABLE status OUTPUT_VARIABLE output ERROR_VARIABLE errors TIMEOUT 600)
    if (NOT status EQUAL 0)
        message(FATAL_ERROR "Self-host hardening artifact build failed (${status}):\n${errors}\n${output}")
    endif()
    vpp_check("${expected}" run --artifact "${artifact}")
    message(STATUS "V++-generated compiler validates ABI and compiles i32 source: PASS")
elseif (VPP_CASE STREQUAL "oop")
    set(fixtures "${VPP_ROOT}/src/tests/fixtures/check_oop_pipeline")
    file(COPY "${VPP_ROOT}/src/tests/fixtures/oop_pipeline/base.vi" DESTINATION "${VPP_OUTPUT_DIR}")
    file(COPY "${fixtures}/api.vi" "${fixtures}/inheritance.vi" DESTINATION "${VPP_OUTPUT_DIR}")
    set(source "${VPP_OUTPUT_DIR}/inheritance.vi")
    set(expected "10\n11\n21\n")
    vpp_check("${expected}" run --compiler=cpp "${source}")
    vpp_check("${expected}" run --compiler=vpp "${source}")
    set(artifact "${VPP_OUTPUT_DIR}/objects.vbc")
    execute_process(COMMAND "${CMAKE_COMMAND}" -E env "VPP_HOME=${VPP_ROOT}"
        "${VPP_EXEC}" build --compiler=vpp "${source}" -o "${artifact}"
        WORKING_DIRECTORY "${VPP_OUTPUT_DIR}"
        RESULT_VARIABLE status ERROR_VARIABLE errors TIMEOUT 240)
    if (NOT status EQUAL 0 OR NOT EXISTS "${artifact}")
        message(FATAL_ERROR "OOP artifact build failed: ${errors}")
    endif()
    file(REMOVE "${source}")
    vpp_check("${expected}" run --artifact "${artifact}")
    file(COPY "${fixtures}/missing_interface.vi" DESTINATION "${VPP_OUTPUT_DIR}")
    execute_process(COMMAND "${CMAKE_COMMAND}" -E env "VPP_HOME=${VPP_ROOT}"
        "${VPP_EXEC}" build --compiler=vpp
        "${VPP_OUTPUT_DIR}/missing_interface.vi" -o "${artifact}"
        RESULT_VARIABLE status TIMEOUT 240)
    if (status EQUAL 0)
        message(FATAL_ERROR "V++ accepted missing interface")
    endif()
    vpp_check("${expected}" run --artifact "${artifact}")
    file(COPY "${fixtures}/stdlib_collection.vi" DESTINATION "${VPP_OUTPUT_DIR}")
    vpp_check("42\n1\n2\n3\n" run --compiler=vpp "${VPP_OUTPUT_DIR}/stdlib_collection.vi")
    message(STATUS "V++ OOP imported interfaces, inheritance and artifact: PASS")
elseif (VPP_CASE STREQUAL "ffi")
    # Exercise the native foreign descriptor table through the V++ compiler,
    # the serialized five-region artifact, and a source-free native replay.
    file(COPY "${VPP_ROOT}/src/tests/kiem_tra_native_ffi_binding.vi"
        DESTINATION "${VPP_OUTPUT_DIR}")
    set(source "${VPP_OUTPUT_DIR}/kiem_tra_native_ffi_binding.vi")
    set(expected "1\n1\n1\n")
    vpp_check("${expected}" run --compiler=vpp "${source}")
    set(artifact "${VPP_OUTPUT_DIR}/ffi.vbc")
    execute_process(COMMAND "${CMAKE_COMMAND}" -E env "VPP_HOME=${VPP_ROOT}"
        "${VPP_EXEC}" build --compiler=vpp "${source}" -o "${artifact}"
        WORKING_DIRECTORY "${VPP_OUTPUT_DIR}"
        RESULT_VARIABLE status OUTPUT_VARIABLE output ERROR_VARIABLE errors TIMEOUT 240)
    if (NOT status EQUAL 0 OR NOT EXISTS "${artifact}")
        message(FATAL_ERROR "Native FFI artifact build failed (${status}): ${errors}\n${output}")
    endif()
    file(READ "${artifact}" contents)
    string(JSON version ERROR_VARIABLE json_error GET "${contents}" "phiên bản")
    if (json_error OR NOT version EQUAL 2)
        message(FATAL_ERROR "Native FFI artifact needs version 2: ${json_error}")
    endif()
    string(JSON regions ERROR_VARIABLE json_error LENGTH "${contents}" "dây")
    if (json_error OR NOT regions EQUAL 5)
        message(FATAL_ERROR "Native FFI artifact needs five wire regions: ${json_error}")
    endif()
    file(REMOVE "${source}")
    vpp_check("${expected}" run --artifact "${artifact}")
    message(STATUS "V++ native FFI and five-region artifact: PASS")
else()
    message(FATAL_ERROR "Unknown V++ CLI test case: ${VPP_CASE}")
endif()
