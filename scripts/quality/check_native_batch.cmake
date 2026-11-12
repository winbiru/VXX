foreach(required IN ITEMS VPP_EXEC VPP_ROOT VPP_NATIVE_COMPILER VPP_OUTPUT_DIR)
    if(NOT DEFINED ${required})
        message(FATAL_ERROR "Native batch check missing ${required}")
    endif()
endforeach()
if(NOT DEFINED VPP_BATCH_CHECK)
    set(VPP_BATCH_CHECK all)
endif()
if(NOT VPP_BATCH_CHECK MATCHES "^(all|parity|source-cache|asm-cache)$")
    message(FATAL_ERROR "Unknown native batch check: ${VPP_BATCH_CHECK}")
endif()
file(MAKE_DIRECTORY "${VPP_OUTPUT_DIR}/expected")
file(REMOVE "${VPP_OUTPUT_DIR}/batch.profile" "${VPP_OUTPUT_DIR}/uncached.profile")
file(WRITE "${VPP_OUTPUT_DIR}/before.vi" "nhập shared.*; hàm chính() { in giá trị(); };\n")
file(WRITE "${VPP_OUTPUT_DIR}/before_repeat.vi" "nhập shared.*; hàm chính() { in giá trị(); };\n")
file(WRITE "${VPP_OUTPUT_DIR}/after.vi" "nhập shared.*; hàm phụ() { trả về 19; }; hàm chính() { in giá trị(); in phụ(); };\n")
# The executable changes an import between requests in the SAME compiler.
# A batch must read it again, and a changed immediate must miss the memo.
file(WRITE "${VPP_OUTPUT_DIR}/change.vi" [=[nhập gói.nhập xuất.tệp.ghi tệp;
hàm chính() { ghi tệp("shared.vi", "hàm giá trị() { trả về 11; };\n"); in "đã đổi 🌿"; };
]=])
file(WRITE "${VPP_OUTPUT_DIR}/bad.vi" "hàm chính( {\n")
file(WRITE "${VPP_OUTPUT_DIR}/runtime.vi" "hàm chính() { a = [1]; in a[4]; };\n")
file(WRITE "${VPP_OUTPUT_DIR}/expected/before.expected" "7\n")
file(WRITE "${VPP_OUTPUT_DIR}/expected/before_repeat.expected" "7\n")
file(WRITE "${VPP_OUTPUT_DIR}/expected/after.expected" "11\n19\n")
set(sources)
foreach(name IN ITEMS before before_repeat change bad runtime after)
    list(APPEND sources "${VPP_OUTPUT_DIR}/${name}.vi")
endforeach()
# Run the *same* source twice before the import changes. The second execution
# must reuse the executable but still produce fresh runtime output/status.
list(INSERT sources 1 "${VPP_OUTPUT_DIR}/before.vi")
set(environment "VPP_HOME=${VPP_ROOT}" "VPP_AOT_ROOT=${VPP_ROOT}"
    "VPP_NATIVE_COMPILER=${VPP_NATIVE_COMPILER}" "VPP_AOT_DIRECT_LINK_CPP=1"
    "VPP_NATIVE_GOLDEN_FAIL_FAST=0" "VPP_COMPILER_PROFILE_FILE=" "VPP_AOT_ASM_DIRECT=1")
if(VPP_BATCH_CHECK MATCHES "^(all|parity)$")
message(STATUS "Native batch: checking isolated/cached/uncached execution")
foreach(mode IN ITEMS single batch uncached)
    message(STATUS "Native batch mode: ${mode}")
    file(MAKE_DIRECTORY "${VPP_OUTPUT_DIR}/${mode}")
    file(WRITE "${VPP_OUTPUT_DIR}/shared.vi" "hàm giá trị() { trả về 7; };\n")
    file(REMOVE "${VPP_OUTPUT_DIR}/${mode}.aot-trace")
    if(mode STREQUAL "single")
        foreach(source IN LISTS sources)
            get_filename_component(name "${source}" NAME_WE)
            execute_process(COMMAND "${CMAKE_COMMAND}" -E env ${environment}
                "VPP_NATIVE_NO_CACHE=1" "VPP_AOT_ASM_DIRECT=0"
                "${VPP_EXEC}" run --compiler=vpp "${source}"
                OUTPUT_VARIABLE output ERROR_VARIABLE errors RESULT_VARIABLE status TIMEOUT 45)
            # Only deliberate syntax/runtime failures may return nonzero.
            # A timeout must fail here, rather than become an oracle result.
            if(NOT status MATCHES "^[01]$")
                message(FATAL_ERROR "Native isolated ${name} failed: ${status}\n${output}\n${errors}")
            endif()
            file(WRITE "${VPP_OUTPUT_DIR}/single/${name}.output" "${output}${errors}")
            file(WRITE "${VPP_OUTPUT_DIR}/single/${name}.status" "${status}\n")
        endforeach()
    else()
        if(mode STREQUAL "uncached")
            set(disabled 1)
        else()
            set(disabled 0)
        endif()
        execute_process(COMMAND "${CMAKE_COMMAND}" -E env ${environment}
            "VPP_NATIVE_NO_CACHE=${disabled}"
            "VPP_AOT_EXECUTABLE_CACHE_DIR=${VPP_OUTPUT_DIR}/${mode}/executables"
            "VPP_AOT_EXECUTABLE_CACHE_TRACE=${VPP_OUTPUT_DIR}/${mode}.aot-trace"
            "VPP_COMPILER_PROFILE_FILE=${VPP_OUTPUT_DIR}/${mode}.profile"
            "${VPP_EXEC}" --batch-run-tests --compiler=vpp "${VPP_OUTPUT_DIR}/${mode}" ${sources}
            RESULT_VARIABLE status OUTPUT_VARIABLE output ERROR_VARIABLE errors TIMEOUT 120)
        if(NOT status STREQUAL "0")
            message(FATAL_ERROR "Native ${mode} worker failed: ${status}\n${output}\n${errors}")
        endif()
        foreach(name IN ITEMS before before_repeat change bad runtime after)
            foreach(extension IN ITEMS output status)
                file(READ "${VPP_OUTPUT_DIR}/single/${name}.${extension}" expected)
                file(READ "${VPP_OUTPUT_DIR}/${mode}/${name}.${extension}" actual)
                string(REPLACE "\r\n" "\n" expected "${expected}")
                string(REPLACE "\r\n" "\n" actual "${actual}")
                if(NOT actual STREQUAL expected)
                    message(FATAL_ERROR "Native ${mode}/${name}.${extension} differs from isolated compiler\nexpected: ${expected}\nactual: ${actual}")
                endif()
            endforeach()
            file(READ "${VPP_OUTPUT_DIR}/${mode}/${name}.milliseconds" elapsed)
            if(NOT elapsed MATCHES "^[0-9]+\n$")
                message(FATAL_ERROR "Native batch missing timing: ${name}")
            endif()
        endforeach()
    endif()
endforeach()
if(NOT EXISTS "${VPP_OUTPUT_DIR}/batch.aot-trace")
    message(FATAL_ERROR "Native batch did not produce executable cache trace")
endif()
file(READ "${VPP_OUTPUT_DIR}/batch.aot-trace" cachedTrace)
if(NOT cachedTrace MATCHES "MISS aot-" OR NOT cachedTrace MATCHES "HIT aot-")
    message(FATAL_ERROR "Native batch must compile once and execute repeated source from cache: ${cachedTrace}")
endif()
if(EXISTS "${VPP_OUTPUT_DIR}/uncached.aot-trace")
    file(READ "${VPP_OUTPUT_DIR}/uncached.aot-trace" disabledTrace)
    if(disabledTrace MATCHES "HIT aot-" OR disabledTrace MATCHES "MISS aot-")
        message(FATAL_ERROR "VPP_NATIVE_NO_CACHE=1 did not disable executable cache")
    endif()
endif()
file(READ "${VPP_OUTPUT_DIR}/batch/before.output" before)
file(READ "${VPP_OUTPUT_DIR}/batch/after.output" after)
string(REPLACE "\r\n" "\n" before "${before}")
string(REPLACE "\r\n" "\n" after "${after}")
if(NOT before STREQUAL "7\n" OR NOT after STREQUAL "11\n19\n")
    message(FATAL_ERROR "Native batch reused stale imported source or leaked function IDs")
endif()
file(READ "${VPP_OUTPUT_DIR}/batch.profile" profile)
if(NOT profile MATCHES "asm.cache.hits [1-9][0-9]*")
    message(FATAL_ERROR "Native batch did not reuse identical Machine IR bodies")
endif()
if(NOT profile MATCHES "source.ast.hits [1-9][0-9]*")
    message(FATAL_ERROR "Native batch did not reuse parsed source ASTs")
endif()
if(NOT profile MATCHES "source.ast.misses [1-9][0-9]*")
    message(FATAL_ERROR "Native batch did not parse changed or new sources")
endif()
# Fail-fast must stop after the first golden mismatch, leaving its evidence.
file(MAKE_DIRECTORY "${VPP_OUTPUT_DIR}/fail-fast")
file(REMOVE "${VPP_OUTPUT_DIR}/fail-fast/after.status")
file(WRITE "${VPP_OUTPUT_DIR}/shared.vi" "hàm giá trị() { trả về 8; };\n")
execute_process(COMMAND "${CMAKE_COMMAND}" -E env ${environment}
    "VPP_NATIVE_GOLDEN_FAIL_FAST=1" "${VPP_EXEC}" --batch-run-tests --compiler=vpp
    "${VPP_OUTPUT_DIR}/fail-fast" "${VPP_OUTPUT_DIR}/before.vi" "${VPP_OUTPUT_DIR}/after.vi"
    RESULT_VARIABLE status OUTPUT_VARIABLE output ERROR_VARIABLE errors TIMEOUT 45)
if(status STREQUAL "0" OR NOT EXISTS "${VPP_OUTPUT_DIR}/fail-fast/before.status" OR
    EXISTS "${VPP_OUTPUT_DIR}/fail-fast/after.status" OR NOT errors MATCHES "GOLDEN FAIL")
    message(FATAL_ERROR "Native batch fail-fast did not stop with evidence: ${status}\n${output}\n${errors}")
endif()
message(STATUS "Native batch: isolated/cached/uncached parity and fail-fast: PASS")
endif()

if(VPP_BATCH_CHECK MATCHES "^(all|source-cache)$")
message(STATUS "Native source cache: checking early hits and import invalidation")
# In a single batch, the second invocation of the exact same entrypoint
# must skip the frontend entirely. If an imported file changes between
# calls, the same entrypoint must compile again and observe the new value.
file(WRITE "${VPP_OUTPUT_DIR}/shared.vi" "hàm giá trị() { trả về 7; };\n")
file(MAKE_DIRECTORY "${VPP_OUTPUT_DIR}/early")
file(REMOVE "${VPP_OUTPUT_DIR}/early.aot-trace")
execute_process(COMMAND "${CMAKE_COMMAND}" -E env ${environment}
    "VPP_NATIVE_NO_CACHE=0"
    "VPP_AOT_EXECUTABLE_CACHE_DIR=${VPP_OUTPUT_DIR}/early/executables"
    "VPP_AOT_EXECUTABLE_CACHE_TRACE=${VPP_OUTPUT_DIR}/early.aot-trace"
    "${VPP_EXEC}" --batch-run-tests --compiler=vpp "${VPP_OUTPUT_DIR}/early"
    "${VPP_OUTPUT_DIR}/before.vi" "${VPP_OUTPUT_DIR}/before.vi"
    "${VPP_OUTPUT_DIR}/change.vi" "${VPP_OUTPUT_DIR}/before.vi"
    RESULT_VARIABLE status OUTPUT_VARIABLE output ERROR_VARIABLE errors TIMEOUT 120)
if(NOT status STREQUAL "0")
    message(FATAL_ERROR "Early executable cache batch failed: ${status}\n${output}\n${errors}")
endif()
file(READ "${VPP_OUTPUT_DIR}/early.aot-trace" earlyTrace)
file(READ "${VPP_OUTPUT_DIR}/early/before.output" earlyOutput)
file(READ "${VPP_OUTPUT_DIR}/early/before.status" earlyStatus)
if(NOT earlyTrace MATCHES "FAST-HIT aot-" OR
    NOT earlyTrace MATCHES "MISS aot-" OR
    NOT earlyOutput STREQUAL "11\n" OR NOT earlyStatus STREQUAL "0\n")
    message(FATAL_ERROR "Early index failed cache-hit/import-edit parity: ${earlyTrace}\n${earlyOutput}")
endif()
# Import resolution is ordered. A previously missing, higher-priority module
# must invalidate the source index even when every old dependency is unchanged.
# Test both a nearer source module and a package entrypoint (chính.vi) that
# shadows an already-resolved gói/<name>.vi module.
foreach(shadow_kind IN ITEMS source package)
    message(STATUS "Native source cache shadow: ${shadow_kind}")
    set(shadow_root "${VPP_OUTPUT_DIR}/shadow-${shadow_kind}")
    set(shadow_app "${shadow_root}/app")
    set(shadow_run "${shadow_root}/results")
    file(MAKE_DIRECTORY "${shadow_app}" "${shadow_run}/pre" "${shadow_run}/post")
    set(shadow_import "nhập shared.*;")
    if(shadow_kind STREQUAL "source")
        file(WRITE "${shadow_root}/shared.vi" "hàm giá trị() { trả về 7; };\n")
        file(REMOVE "${shadow_app}/shared.vi")
        set(shadow_new "shared.vi")
    else()
        set(shadow_import "nhập shadow_pkg.*;")
        file(MAKE_DIRECTORY "${shadow_app}/gói/shadow_pkg")
        file(WRITE "${shadow_app}/gói/shadow_pkg.vi" "hàm giá trị() { trả về 7; };\n")
        file(REMOVE "${shadow_app}/gói/shadow_pkg/chính.vi")
        set(shadow_new "gói/shadow_pkg/chính.vi")
    endif()
    file(WRITE "${shadow_app}/resolve.vi" "${shadow_import} hàm chính() { in giá trị(); };\n")
    file(WRITE "${shadow_app}/introduce.vi" "nhập gói.nhập xuất.tệp.ghi tệp;\n"
        "hàm chính() { ghi tệp(\"${shadow_new}\", \"hàm giá trị() { trả về 11; };\\n\"); };\n")
    file(REMOVE_RECURSE "${shadow_run}/executables")
    file(REMOVE "${shadow_run}/aot-trace")
    set(shadow_env ${environment}
        "VPP_NATIVE_NO_CACHE=0"
        "VPP_AOT_EXECUTABLE_CACHE_DIR=${shadow_run}/executables"
        "VPP_AOT_EXECUTABLE_CACHE_TRACE=${shadow_run}/aot-trace")
    execute_process(COMMAND "${CMAKE_COMMAND}" -E env ${shadow_env}
        "${VPP_EXEC}" --batch-run-tests --compiler=vpp "${shadow_run}/pre"
        "${shadow_app}/resolve.vi" "${shadow_app}/resolve.vi"
        RESULT_VARIABLE status OUTPUT_VARIABLE output ERROR_VARIABLE errors TIMEOUT 120)
    if(NOT status STREQUAL "0")
        message(FATAL_ERROR "${shadow_kind} initial cache batch failed: ${status}\n${output}\n${errors}")
    endif()
    file(READ "${shadow_run}/pre/resolve.output" shadow_before)
    file(READ "${shadow_run}/pre/resolve.status" shadow_status)
    file(READ "${shadow_run}/aot-trace" shadow_trace_before)
    if(NOT shadow_before STREQUAL "7\n" OR NOT shadow_status STREQUAL "0\n" OR
       NOT shadow_trace_before MATCHES "FAST-HIT aot-")
        message(FATAL_ERROR "${shadow_kind} initial resolution/cache failed: ${shadow_before}\n${shadow_trace_before}")
    endif()
    execute_process(COMMAND "${CMAKE_COMMAND}" -E env ${shadow_env}
        "${VPP_EXEC}" --batch-run-tests --compiler=vpp "${shadow_run}/post"
        "${shadow_app}/introduce.vi" "${shadow_app}/resolve.vi" "${shadow_app}/resolve.vi"
        RESULT_VARIABLE status OUTPUT_VARIABLE output ERROR_VARIABLE errors TIMEOUT 120)
    if(NOT status STREQUAL "0")
        message(FATAL_ERROR "${shadow_kind} shadow batch failed: ${status}\n${output}\n${errors}")
    endif()
    file(READ "${shadow_run}/post/resolve.output" shadow_after)
    file(READ "${shadow_run}/post/resolve.status" shadow_status)
    if(NOT shadow_after STREQUAL "11\n" OR NOT shadow_status STREQUAL "0\n")
        message(FATAL_ERROR "${shadow_kind} cache reused a shadowed import: ${shadow_after}")
    endif()
    # The changed dependency graph should get a new executable; the third
    # request to the same entrypoint can then take the fast-cache path.
    file(READ "${shadow_run}/aot-trace" shadow_trace_after)
    string(REPLACE "${shadow_trace_before}" "" shadow_delta "${shadow_trace_after}")
    if(NOT shadow_delta MATCHES "MISS aot-" OR NOT shadow_delta MATCHES "FAST-HIT aot-")
        message(FATAL_ERROR "${shadow_kind} shadow invalidation trace missing: ${shadow_delta}")
    endif()
endforeach()
message(STATUS "Native source cache: early hits, source edits and import shadowing: PASS")
endif()

if(VPP_BATCH_CHECK MATCHES "^(all|asm-cache)$")
message(STATUS "Native ASM cache: checking cold/warm/disabled execution")
execute_process(COMMAND "${CMAKE_COMMAND}" -E env ${environment}
    "${VPP_EXEC}" run --compiler=vpp "${VPP_ROOT}/src/tests/kiem_tra_asm_cache.vi"
    RESULT_VARIABLE status OUTPUT_VARIABLE output ERROR_VARIABLE errors TIMEOUT 120)
if(NOT status STREQUAL "0" OR NOT output MATCHES "ASM cache parity: đạt")
    message(FATAL_ERROR "Cold/warm/disabled ASM parity failed: ${status}\n${output}\n${errors}")
endif()
message(STATUS "Native ASM cache: cold/warm/disabled parity: PASS")
endif()
