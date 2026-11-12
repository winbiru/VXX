foreach(required IN ITEMS VPP_EXEC VPP_ROOT VPP_NATIVE_COMPILER VPP_OUTPUT_DIR)
    if(NOT DEFINED ${required})
        message(FATAL_ERROR "Native linker cache check missing ${required}")
    endif()
endforeach()

file(MAKE_DIRECTORY "${VPP_OUTPUT_DIR}")
file(WRITE "${VPP_OUTPUT_DIR}/first.vi" "hàm chính() { in 11; };\n")
file(WRITE "${VPP_OUTPUT_DIR}/second.vi" "hàm chính() { in 22; };\n")
# Opt out of the macOS direct Stage 0 linker so the CMake fallback is
# exercised on every host, including Visual Studio on Windows. Each mode runs
# two distinct native binaries
# in one self-hosted compiler session; cached and uncached output must agree.
foreach(mode IN ITEMS cached uncached)
    file(MAKE_DIRECTORY "${VPP_OUTPUT_DIR}/${mode}")
    if(mode STREQUAL "cached")
        set(enable_cache 1)
    else()
        set(enable_cache 0)
    endif()
    execute_process(COMMAND "${CMAKE_COMMAND}" -E env
        "VPP_HOME=${VPP_ROOT}" "VPP_AOT_ROOT=${VPP_ROOT}"
        "VPP_NATIVE_COMPILER=${VPP_NATIVE_COMPILER}"
        "VPP_AOT_DIRECT_LINK_CPP=0" "VPP_NATIVE_PROFILE=1"
        "VPP_AOT_LINKER_CACHE=${enable_cache}"
        "${VPP_EXEC}" --batch-run-tests --compiler=vpp "${VPP_OUTPUT_DIR}/${mode}"
        "${VPP_OUTPUT_DIR}/first.vi" "${VPP_OUTPUT_DIR}/second.vi"
        RESULT_VARIABLE status OUTPUT_VARIABLE output ERROR_VARIABLE errors TIMEOUT 120)
    if(NOT status STREQUAL "0")
        message(FATAL_ERROR "Native linker ${mode} batch failed: ${status}\n${output}\n${errors}")
    endif()

    foreach(name IN ITEMS first second)
        if(NOT EXISTS "${VPP_OUTPUT_DIR}/${mode}/${name}.status" OR
           NOT EXISTS "${VPP_OUTPUT_DIR}/${mode}/${name}.output")
            message(FATAL_ERROR "Native linker ${mode} lost ${name} evidence\n${errors}")
        endif()
        file(READ "${VPP_OUTPUT_DIR}/${mode}/${name}.status" fixture_status)
        file(READ "${VPP_OUTPUT_DIR}/${mode}/${name}.output" fixture_output)
        string(REPLACE "\r\n" "\n" fixture_status "${fixture_status}")
        string(REPLACE "\r\n" "\n" fixture_output "${fixture_output}")
        string(REGEX REPLACE "VPP_NATIVE_PROFILE [^\n]*\n" "" actual "${fixture_output}")
        if(NOT fixture_output MATCHES "VPP_NATIVE_PROFILE driver-aot-vpp [0-9]+ ms")
            message(FATAL_ERROR "Native linker bypassed the V++ AOT driver: ${mode}/${name}\n${fixture_output}")
        endif()
        if(name STREQUAL "first")
            set(expected "11\n")
        else()
            set(expected "22\n")
        endif()
        if(NOT fixture_output MATCHES "VPP_NATIVE_PROFILE configure-native-linker [0-9]+ ms" AND
           (name STREQUAL "first" OR mode STREQUAL "uncached"))
            message(FATAL_ERROR "Native linker ${mode}/${name} did not configure: ${fixture_output}")
        endif()
        if(name STREQUAL "second")
            if(mode STREQUAL "cached")
                if(NOT fixture_output MATCHES "VPP_NATIVE_PROFILE reuse-native-linker [0-9]+ ms" OR
                   fixture_output MATCHES "VPP_NATIVE_PROFILE configure-native-linker")
                    message(FATAL_ERROR "Native linker ${mode} did not reuse: ${fixture_output}")
                endif()
            elseif(fixture_output MATCHES "VPP_NATIVE_PROFILE reuse-native-linker")
                message(FATAL_ERROR "Native linker cache opt-out was ignored: ${fixture_output}")
            endif()
        endif()
        if(NOT fixture_status STREQUAL "0\n" OR NOT actual STREQUAL expected)
            message(FATAL_ERROR "Native linker ${mode} compiled stale program: ${name} (status=${fixture_status}, output=${actual})")
        endif()
    endforeach()
endforeach()

# The supported Linux and macOS Stage 0 routes assemble and link directly.
# Compile the same programs through both linkers and verify the selected
# pipeline from its profiling marker as well as the executable output.
if((APPLE AND CMAKE_HOST_SYSTEM_PROCESSOR MATCHES "^(arm64|aarch64)$") OR
   (CMAKE_HOST_SYSTEM_NAME STREQUAL "Linux" AND
    CMAKE_HOST_SYSTEM_PROCESSOR MATCHES "^(x86_64|amd64|aarch64|arm64)$"))
    foreach(mode IN ITEMS direct cmake)
        if(mode STREQUAL "direct")
            set(direct_setting "VPP_AOT_DIRECT_LINK_CPP=")
            set(profile_marker "assemble-stage0-program")
            set(forbidden_marker "configure-native-linker")
        else()
            set(direct_setting "VPP_AOT_DIRECT_LINK_CPP=0")
            set(profile_marker "configure-native-linker")
            set(forbidden_marker "assemble-stage0-program")
        endif()
        foreach(name IN ITEMS first second)
            set(executable "${VPP_OUTPUT_DIR}/${mode}-${name}")
            execute_process(COMMAND "${CMAKE_COMMAND}" -E env
                "VPP_HOME=${VPP_ROOT}" "VPP_AOT_ROOT=${VPP_ROOT}"
                "VPP_AOT_RUNTIME=cpp" "VPP_NATIVE_PROFILE=1" "${direct_setting}"
                "${VPP_EXEC}" --emit-native "${VPP_OUTPUT_DIR}/${name}.vi" "${executable}"
                RESULT_VARIABLE status OUTPUT_VARIABLE build_output
                ERROR_VARIABLE errors TIMEOUT 60)
            if(NOT status STREQUAL "0" OR NOT EXISTS "${executable}" OR
               NOT errors MATCHES "VPP_NATIVE_PROFILE ${profile_marker}" OR
               errors MATCHES "VPP_NATIVE_PROFILE ${forbidden_marker}")
                message(FATAL_ERROR "Native ${mode}/${name}: wrong linker route or build failure (${status})\n${build_output}\n${errors}")
            endif()
            execute_process(COMMAND "${executable}"
                RESULT_VARIABLE status OUTPUT_VARIABLE actual ERROR_VARIABLE errors TIMEOUT 15)
            if(name STREQUAL "first")
                set(expected "11\n")
            else()
                set(expected "22\n")
            endif()
            if(NOT status STREQUAL "0" OR NOT actual STREQUAL expected)
                message(FATAL_ERROR "Native ${mode}/${name}: program mismatch (${status}): ${actual}\n${errors}")
            endif()
        endforeach()
    endforeach()
endif()

message(STATUS "Native linker: cached/uncached parity, correct reuse, two distinct native binaries: PASS")
