foreach(required IN ITEMS VPP_ROOT VPP_NATIVE_COMPILER VPP_OUTPUT_DIR VPP_EXECUTABLE_SUFFIX)
    if(NOT DEFINED ${required})
        message(FATAL_ERROR "V++ AOT driver check missing ${required}")
    endif()
endforeach()
# Every literal native callback needs a stable top-level V++ entrypoint;
# imported module-private names are not sufficient for invokeName().
file(READ "${VPP_ROOT}/src/native/aot_driver.cpp" native_host)
file(READ "${VPP_ROOT}/gói/biên dịch/dòng lệnh bản địa.vi" native_entrypoints)
string(REGEX MATCHALL "invokeName\\(\"[^\"]+\"" callbacks "${native_host}")
foreach(callback IN LISTS callbacks)
    string(REGEX REPLACE "invokeName\\(\"([^\"]+)\"" "\\1" name "${callback}")
    string(FIND "${native_entrypoints}" "hàm ${name}(" entrypoint)
    if(entrypoint LESS 0)
        message(FATAL_ERROR "Native AOT callback has no V++ entrypoint: ${name}")
    endif()
endforeach()
file(MAKE_DIRECTORY "${VPP_OUTPUT_DIR}")
# Leftovers from an interrupted older run do not belong to this test run.
file(GLOB preexisting_temporary "${VPP_OUTPUT_DIR}/.vpp-aot-*")
set(base_env "VPP_HOME=${VPP_ROOT}" "VPP_AOT_ROOT=${VPP_ROOT}"
    "VPP_AOT_RUNTIME=cpp" "VPP_AOT_VPP_OBJECT=" "VPP_AOT_DIRECT_LINK_CPP="
    "VPP_AOT_WINDOWS_DIRECT_LINK=0" "VPP_AOT_ASSEMBLER=" "VPP_AOT_C_COMPILER="
    "VPP_NATIVE_PROFILE=1")
set(probe "${VPP_OUTPUT_DIR}/driver-probe${VPP_EXECUTABLE_SUFFIX}")
execute_process(COMMAND "${CMAKE_COMMAND}" -E env ${base_env}
    "${VPP_NATIVE_COMPILER}" build "${VPP_ROOT}/src/tests/fixtures/native_aot_driver_plan.vi" -o "${probe}"
    RESULT_VARIABLE status OUTPUT_VARIABLE output ERROR_VARIABLE errors TIMEOUT 120)
if(NOT status STREQUAL "0" OR NOT errors MATCHES "VPP_NATIVE_PROFILE driver-aot-vpp")
    message(FATAL_ERROR "V++ driver probe build failed or bypassed V++ driver: ${status}\n${output}\n${errors}")
endif()

# Cache keys must remain byte-identical after moving SHA-256 word arithmetic
# to V++ bit operators. Cover padding, binary bytes, Unicode and long HMAC keys.
set(hash_probe "${VPP_OUTPUT_DIR}/hash-probe${VPP_EXECUTABLE_SUFFIX}")
execute_process(COMMAND "${CMAKE_COMMAND}" -E env ${base_env}
    "${VPP_NATIVE_COMPILER}" build "${VPP_ROOT}/src/tests/fixtures/native_aot_hash.vi" -o "${hash_probe}"
    RESULT_VARIABLE status OUTPUT_VARIABLE output ERROR_VARIABLE errors TIMEOUT 120)
if(NOT status STREQUAL "0")
    message(FATAL_ERROR "V++ SHA-256 probe build failed: ${status}\n${output}\n${errors}")
endif()
execute_process(COMMAND "${hash_probe}"
    RESULT_VARIABLE status OUTPUT_VARIABLE output ERROR_VARIABLE errors TIMEOUT 30)
if(NOT status STREQUAL "0" OR NOT output MATCHES "AOT_HASH_OK")
    message(FATAL_ERROR "V++ SHA-256 parity failed: ${status}\n${output}\n${errors}")
endif()

# The native probe contains the actual V++ driver. Exercise platform policy
# on all hosts without claiming that foreign executables have been run.
set(fake "${VPP_OUTPUT_DIR}/argv spaces 🌿 $(literal)")
file(MAKE_DIRECTORY "${fake}/libraries/lib" "${fake}/build/native-vpp-runtime")
file(WRITE "${fake}/libraries/libssl.a" "static archive probe")
file(WRITE "${fake}/libraries/libcrypto.a" "static archive probe")
file(WRITE "${fake}/libraries/CMakeCache.txt"
    "OPENSSL_SSL_LIBRARY:FILEPATH=${fake}/libraries/libssl.a\n"
    "OPENSSL_CRYPTO_LIBRARY:FILEPATH=${fake}/libraries/libcrypto.a\n"
    "VPP_OPENSSL_STATIC_DEPS:STRING=-lz;-lzstd;-ljitterentropy\n")
file(WRITE "${fake}/build/native-vpp-runtime/runtime.o" "runtime probe")
foreach(module IN ITEMS native-aot runtime-native compiler frontend bytecode core)
    file(WRITE "${fake}/libraries/lib/libvpp-${module}.a" "archive probe")
    file(WRITE "${fake}/libraries/lib/vpp-${module}.lib" "archive probe")
endforeach()
execute_process(COMMAND "${CMAKE_COMMAND}" -E env
    "VPP_AOT_DRIVER_FIXTURE_ROOT=${fake}" "VPP_AOT_DRIVER_FIXTURE_MODE=os-file"
    "VPP_AOT_LIB_ROOT=${fake}/libraries" "VPP_COMPILER_PROFILE_FILE=${fake}/profile.json"
    "VPP_NATIVE_COMPILER=${fake}/build/native-vpp-runtime/runtime.o"
    "${probe}"
    RESULT_VARIABLE status OUTPUT_VARIABLE output ERROR_VARIABLE errors TIMEOUT 30)
if(NOT status STREQUAL "0" OR NOT output MATCHES "AOT_DRIVER_OK")
    message(FATAL_ERROR "V++ driver OS/file policy: ${status}\n${output}\n${errors}")
endif()
foreach(case IN ITEMS
    "aarch64-apple-darwin#cpp#direct"
    "x86_64-unknown-linux-gnu#cpp#direct"
    "aarch64-unknown-linux-gnu#cpp#direct"
    "x86_64-pc-windows-msvc#cpp#windows"
    "aarch64-apple-darwin#vpp#vpp"
    "x86_64-unknown-linux-gnu#vpp-freestanding#freestanding"
    "aarch64-unknown-linux-gnu#vpp#reject"
    "aarch64-unknown-linux-gnu#vpp-freestanding#reject"
    "aarch64-apple-darwin#unknown#reject"
    "aarch64-apple-darwin#cpp#cmake"
    "x86_64-pc-windows-msvc#cpp#launch-error")
    string(REPLACE "#" ";" parts "${case}")
    list(GET parts 0 target)
    list(GET parts 1 runtime)
    list(GET parts 2 mode)
    set(direct 1)
    if(mode STREQUAL "cmake")
        set(direct 0)
    endif()
    execute_process(COMMAND "${CMAKE_COMMAND}" -E env
        "VPP_AOT_DRIVER_FIXTURE_ROOT=${fake}" "VPP_AOT_DRIVER_FIXTURE_TARGET=${target}"
        "VPP_AOT_DRIVER_FIXTURE_MODE=${mode}" "VPP_AOT_RUNTIME=${runtime}"
        "VPP_AOT_DIRECT_LINK_CPP=${direct}" "VPP_AOT_WINDOWS_DIRECT_LINK=1"
        "VPP_AOT_DIRECT_LINK=1" "VPP_AOT_VPP_OBJECT=" "VPP_AOT_LINKER_CACHE=0"
        "${probe}"
        RESULT_VARIABLE status OUTPUT_VARIABLE output ERROR_VARIABLE errors TIMEOUT 30)
    if(NOT status STREQUAL "0" OR NOT output MATCHES "AOT_DRIVER_(REJECT_)?OK")
        message(FATAL_ERROR "V++ driver policy ${case}: ${status}\n${output}\n${errors}")
    endif()
endforeach()

# Compile the complete driver and compiler through V++ itself, then use the
# resulting Stage 2 compiler to build and run an independent program.
set(stage2 "${VPP_OUTPUT_DIR}/vpp-stage2${VPP_EXECUTABLE_SUFFIX}")
execute_process(COMMAND "${CMAKE_COMMAND}" -E env ${base_env}
    "${VPP_NATIVE_COMPILER}" build "${VPP_ROOT}/gói/biên dịch/dòng lệnh bản địa.vi" --compiler -o "${stage2}"
    RESULT_VARIABLE status OUTPUT_VARIABLE output ERROR_VARIABLE errors TIMEOUT 180)
if(NOT status STREQUAL "0" OR NOT errors MATCHES "VPP_NATIVE_PROFILE driver-aot-vpp")
    message(FATAL_ERROR "V++ driver self-host failed: ${status}\n${output}\n${errors}")
endif()
file(WRITE "${VPP_OUTPUT_DIR}/source with spaces 🌿.vi" "hàm chính() { in 42; };\n")
set(executable "${VPP_OUTPUT_DIR}/result with spaces 🌿${VPP_EXECUTABLE_SUFFIX}")
execute_process(COMMAND "${CMAKE_COMMAND}" -E env ${base_env}
    "${stage2}" build "${VPP_OUTPUT_DIR}/source with spaces 🌿.vi" -o "${executable}"
    RESULT_VARIABLE status OUTPUT_VARIABLE output ERROR_VARIABLE errors TIMEOUT 60)
if(NOT status STREQUAL "0" OR NOT errors MATCHES "VPP_NATIVE_PROFILE driver-aot-vpp")
    message(FATAL_ERROR "Stage 2 did not use the V++ AOT driver: ${status}\n${output}\n${errors}")
endif()
execute_process(COMMAND "${executable}" RESULT_VARIABLE status OUTPUT_VARIABLE output ERROR_VARIABLE errors TIMEOUT 15)
string(REPLACE "\r\n" "\n" output "${output}")
if(NOT status STREQUAL "0" OR NOT output STREQUAL "42\n")
    message(FATAL_ERROR "Stage 2 executable mismatch: ${status}\n${output}\n${errors}")
endif()

# Path identity policy belongs to V++ and must protect source files even
# when the output is a hard link with a different pathname.
set(source "${VPP_OUTPUT_DIR}/source with spaces 🌿.vi")
set(alias "${VPP_OUTPUT_DIR}/source alias.vi")
file(REMOVE "${alias}")
file(CREATE_LINK "${source}" "${alias}" RESULT link_error)
if(link_error STREQUAL "0")
    file(SHA256 "${source}" source_before)
    execute_process(COMMAND "${CMAKE_COMMAND}" -E env ${base_env}
        "${stage2}" build "${source}" -o "${alias}"
        RESULT_VARIABLE status OUTPUT_VARIABLE output ERROR_VARIABLE errors TIMEOUT 30)
    file(SHA256 "${source}" source_after)
    if(status STREQUAL "0" OR NOT source_before STREQUAL source_after OR
        NOT errors MATCHES "output must not overwrite source")
        message(FATAL_ERROR "V++ output/source identity protection failed: ${status}\n${output}\n${errors}")
    endif()
endif()

# Failed tool invocations must report the V++ error and preserve an existing
# destination. A quoted path must remain one argv value, with no shell eval.
file(SHA256 "${executable}" before)
execute_process(COMMAND "${CMAKE_COMMAND}" -E env ${base_env}
    "VPP_AOT_WINDOWS_DIRECT_LINK=1"
    "VPP_AOT_ASSEMBLER=${VPP_OUTPUT_DIR}/missing assembler $(literal)"
    "${stage2}" build "${VPP_OUTPUT_DIR}/source with spaces 🌿.vi" -o "${executable}"
    RESULT_VARIABLE status OUTPUT_VARIABLE output ERROR_VARIABLE errors TIMEOUT 30)
file(SHA256 "${executable}" after)
if(status STREQUAL "0" OR NOT before STREQUAL after OR NOT errors MATCHES "Native AOT: .*assembly failed")
    message(FATAL_ERROR "V++ driver lost tool error/output atomicity: ${status}\n${output}\n${errors}")
endif()
file(GLOB leftovers "${VPP_OUTPUT_DIR}/.vpp-aot-*")
foreach(previous IN LISTS preexisting_temporary)
    list(REMOVE_ITEM leftovers "${previous}")
endforeach()
if(leftovers)
    message(FATAL_ERROR "V++ driver left temporary files: ${leftovers}")
endif()
message(STATUS "V++ AOT driver: platform plans, OS/files, launch failure, self-hosted Stage 2, Unicode argv, output preservation: PASS")
