foreach(required IN ITEMS VPP_NATIVE_CLI VPP_STAGE0 VPP_ROOT VPP_OUTPUT_DIR)
    if (NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "Native object CLI missing ${required}")
    endif()
endforeach()
file(MAKE_DIRECTORY "${VPP_OUTPUT_DIR}/tệp có dấu")
set(source "${VPP_OUTPUT_DIR}/tệp có dấu/kernels.vi")
configure_file("${VPP_ROOT}/benchmark/native/kernels.vi" "${source}" COPYONLY)

# An object compiler already contains the V++ parser, lowering, encoders and
# writers. Empty PATH and unset toolchain roots prove this route needs neither
# Stage 0 nor cmake/clang/a wrapper subprocess after bootstrap.
set(native_command "${CMAKE_COMMAND}" -E env
    --unset=VPP_HOME --unset=VPP_AOT_ROOT --unset=VPP_AOT_LIB_ROOT
    "PATH=" "VPP_NATIVE_COMPILER=/missing-stage0"
    "${VPP_NATIVE_CLI}")
execute_process(COMMAND ${native_command} --help
    RESULT_VARIABLE status OUTPUT_VARIABLE output ERROR_VARIABLE errors TIMEOUT 30)
if (NOT status STREQUAL "0" OR NOT output MATCHES "emit-obj-selfhost")
    message(FATAL_ERROR "V++ CLI help failed: ${status} ${output} ${errors}")
endif()

set(checked 0)
foreach(target IN ITEMS aarch64-apple-darwin aarch64-linux-gnu
                        x86_64-linux-gnu x86_64-windows-msvc)
    foreach(kernel IN ITEMS vpp_arithmetic vpp_cycle vpp_fibonacci)
        set(object "${VPP_OUTPUT_DIR}/${target}-${kernel}.o")
        set(reference "${VPP_OUTPUT_DIR}/reference-${target}-${kernel}.o")
        execute_process(COMMAND ${native_command} build --target "${target}"
            --emit-obj-selfhost "${kernel}" "${source}" "${object}"
            WORKING_DIRECTORY "${VPP_OUTPUT_DIR}"
            RESULT_VARIABLE status OUTPUT_VARIABLE output ERROR_VARIABLE errors TIMEOUT 60)
        if (NOT status STREQUAL "0" OR NOT EXISTS "${object}")
            message(FATAL_ERROR "V++ CLI object ${target}/${kernel}: ${status} ${output} ${errors}")
        endif()
        execute_process(COMMAND "${CMAKE_COMMAND}" -E env "VPP_HOME=${VPP_ROOT}"
            "${VPP_STAGE0}" build --target "${target}" --emit-obj-selfhost
            "${kernel}" "${source}" "${reference}"
            WORKING_DIRECTORY "${VPP_ROOT}"
            RESULT_VARIABLE status OUTPUT_VARIABLE output ERROR_VARIABLE errors TIMEOUT 90)
        if (NOT status STREQUAL "0" OR NOT EXISTS "${reference}")
            message(FATAL_ERROR "Stage 0 reference ${target}/${kernel}: ${status} ${output} ${errors}")
        endif()
        file(SHA256 "${object}" actual_hash)
        file(SHA256 "${reference}" reference_hash)
        if (NOT actual_hash STREQUAL reference_hash)
            message(FATAL_ERROR "V++ CLI object differs from Stage 0: ${target}/${kernel}")
        endif()
        # The ordinary --emit-obj command must use the same V++ writer.
        set(ordinary "${VPP_OUTPUT_DIR}/ordinary-${target}-${kernel}.o")
        execute_process(COMMAND ${native_command} build --target "${target}"
            --emit-obj "${kernel}" "${source}" "${ordinary}"
            RESULT_VARIABLE status OUTPUT_VARIABLE output ERROR_VARIABLE errors TIMEOUT 60)
        if (NOT status STREQUAL "0" OR NOT EXISTS "${ordinary}")
            message(FATAL_ERROR "Native --emit-obj failed: ${target}/${kernel}: ${output} ${errors}")
        endif()
        file(SHA256 "${ordinary}" ordinary_hash)
        if (NOT ordinary_hash STREQUAL actual_hash)
            message(FATAL_ERROR "Native --emit-obj disagrees with selfhost: ${target}/${kernel}")
        endif()
        math(EXPR checked "${checked} + 1")
    endforeach()
endforeach()

# Options may precede the source and -o also works with the object mode.
set(alternate "${VPP_OUTPUT_DIR}/alternate.o")
execute_process(COMMAND ${native_command} build -o "${alternate}"
    --emit-obj-selfhost vpp_cycle --target aarch64-apple-darwin "${source}"
    RESULT_VARIABLE status OUTPUT_VARIABLE output ERROR_VARIABLE errors TIMEOUT 60)
if (NOT status STREQUAL "0")
    message(FATAL_ERROR "V++ CLI -o failed: ${status} ${output} ${errors}")
endif()
file(SHA256 "${alternate}" actual_hash)
file(SHA256 "${VPP_OUTPUT_DIR}/aarch64-apple-darwin-vpp_cycle.o" reference_hash)
if (NOT actual_hash STREQUAL reference_hash)
    message(FATAL_ERROR "CLI option order changed object bytes")
endif()

# The compatibility CLI must delegate before parsing the target through the
# Stage 0 bridge. A configured compiler's failure is propagated without fallback.
set(delegated "${VPP_OUTPUT_DIR}/delegated.o")
execute_process(COMMAND "${CMAKE_COMMAND}" -E env "PATH="
    "VPP_NATIVE_COMPILER=${VPP_NATIVE_CLI}" "VPP_NATIVE_STAGE0_PROFILE=1"
    "${VPP_STAGE0}" build --target aarch64-apple-darwin --emit-obj-selfhost
    vpp_cycle "${source}" "${delegated}"
    RESULT_VARIABLE status OUTPUT_VARIABLE output ERROR_VARIABLE errors TIMEOUT 30)
if (NOT status STREQUAL "0" OR errors MATCHES "native Stage 0")
    message(FATAL_ERROR "Compatibility CLI used Stage 0 bridge: ${status} ${output} ${errors}")
endif()
file(SHA256 "${delegated}" actual_hash)
if (NOT actual_hash STREQUAL reference_hash)
    message(FATAL_ERROR "Compatibility CLI changed native object")
endif()
set(regular_delegated "${VPP_OUTPUT_DIR}/regular-delegated.o")
execute_process(COMMAND "${CMAKE_COMMAND}" -E env "PATH="
    "VPP_NATIVE_COMPILER=${VPP_NATIVE_CLI}" "VPP_NATIVE_STAGE0_PROFILE=1"
    "${VPP_STAGE0}" build --target aarch64-apple-darwin --emit-obj
    vpp_cycle "${source}" "${regular_delegated}"
    RESULT_VARIABLE status OUTPUT_VARIABLE output ERROR_VARIABLE errors TIMEOUT 30)
if (NOT status STREQUAL "0" OR errors MATCHES "native Stage 0")
    message(FATAL_ERROR "--emit-obj used Stage 0 bridge: ${status} ${output} ${errors}")
endif()
file(SHA256 "${regular_delegated}" ordinary_hash)
if (NOT ordinary_hash STREQUAL reference_hash)
    message(FATAL_ERROR "--emit-obj delegation changed native object")
endif()

# The raw leaf encoder is entirely V++ and preserves the old ABI bytes.
set(raw "${VPP_OUTPUT_DIR}/leaf-raw.bin")
set(leaf "${VPP_ROOT}/src/tests/kiem_tra_native_leaf.vi")
execute_process(COMMAND ${native_command} build --target x86_64-linux-gnu
    --emit-raw cong_dao "${leaf}" "${raw}"
    RESULT_VARIABLE status OUTPUT_VARIABLE output ERROR_VARIABLE errors TIMEOUT 60)
if (NOT status STREQUAL "0")
    message(FATAL_ERROR "Native raw leaf failed: ${status} ${output} ${errors}")
endif()
file(READ "${raw}" bytes HEX)
if (NOT bytes STREQUAL "89f801f0c3")
    message(FATAL_ERROR "Native leaf ABI changed: ${bytes}")
endif()
execute_process(COMMAND "${CMAKE_COMMAND}" -E env "PATH="
    "VPP_NATIVE_COMPILER=${VPP_NATIVE_CLI}" "VPP_NATIVE_STAGE0_PROFILE=1"
    "${VPP_STAGE0}" build --target riscv64-linux-gnu --emit-raw
    tra42 "${leaf}" "${raw}"
    RESULT_VARIABLE status OUTPUT_VARIABLE output ERROR_VARIABLE errors TIMEOUT 30)
if (NOT status STREQUAL "0" OR errors MATCHES "native Stage 0")
    message(FATAL_ERROR "Raw RISC-V dispatch still used Stage 0: ${status} ${output} ${errors}")
endif()
file(READ "${raw}" bytes HEX)
if (NOT bytes STREQUAL "370500001b05a50267800000")
    message(FATAL_ERROR "Native RISC-V leaf ABI changed: ${bytes}")
endif()
set(assembly "${VPP_OUTPUT_DIR}/leaf-assembly.s")
execute_process(COMMAND "${CMAKE_COMMAND}" -E env "PATH="
    "VPP_NATIVE_COMPILER=${VPP_NATIVE_CLI}" "VPP_NATIVE_STAGE0_PROFILE=1"
    "${VPP_STAGE0}" build --target riscv64-linux-gnu --emit-asm
    tra42 "${leaf}" "${assembly}"
    RESULT_VARIABLE status OUTPUT_VARIABLE output ERROR_VARIABLE errors TIMEOUT 30)
if (NOT status STREQUAL "0" OR errors MATCHES "native Stage 0")
    message(FATAL_ERROR "Assembly dispatch still used Stage 0: ${status} ${output} ${errors}")
endif()
file(READ "${assembly}" listing)
if (NOT listing MATCHES "lui a0, 0" OR NOT listing MATCHES "addiw a0, a0, 42" OR
    NOT listing MATCHES "jalr zero, ra, 0")
    message(FATAL_ERROR "Native RISC-V assembly printer regressed: ${listing}")
endif()
set(general_assembly "${VPP_OUTPUT_DIR}/machine-call.s")
execute_process(COMMAND ${native_command} build --target aarch64-apple-darwin
    --emit-asm chua_ho_tro "${leaf}" "${general_assembly}"
    RESULT_VARIABLE status OUTPUT_VARIABLE output ERROR_VARIABLE errors TIMEOUT 60)
if (NOT status STREQUAL "0" OR NOT EXISTS "${general_assembly}")
    message(FATAL_ERROR "Native Machine IR assembly failed: ${status} ${output} ${errors}")
endif()
file(READ "${general_assembly}" listing)
if (NOT listing MATCHES "bl " OR NOT listing MATCHES "_chua_ho_tro:")
    message(FATAL_ERROR "Native Machine IR assembly lost CALL or function symbol: ${listing}")
endif()
execute_process(COMMAND "${CMAKE_COMMAND}" -E env "PATH="
    "VPP_NATIVE_COMPILER=/missing-compiler"
    "${VPP_STAGE0}" build --target aarch64-apple-darwin --emit-obj-selfhost
    vpp_cycle "${source}" "${delegated}"
    RESULT_VARIABLE status OUTPUT_VARIABLE output ERROR_VARIABLE errors TIMEOUT 30)
file(SHA256 "${delegated}" after_hash)
if (status STREQUAL "0" OR NOT after_hash STREQUAL actual_hash)
    message(FATAL_ERROR "Compatibility CLI silently fell back or overwrote output")
endif()

set(preserved "${VPP_OUTPUT_DIR}/preserved.o")
function(reject label)
    file(WRITE "${preserved}" "keep existing output")
    execute_process(COMMAND ${native_command} ${ARGN}
        WORKING_DIRECTORY "${VPP_OUTPUT_DIR}"
        RESULT_VARIABLE status OUTPUT_VARIABLE output ERROR_VARIABLE errors TIMEOUT 30)
    file(READ "${preserved}" content)
    if (status STREQUAL "0" OR NOT content STREQUAL "keep existing output")
        message(FATAL_ERROR "CLI must reject ${label} and preserve output: ${status} ${output} ${errors}")
    endif()
endfunction()
reject("unknown option" build --bogus "${source}" -o "${preserved}")
reject("missing flag value" build "${source}" -o)
reject("duplicate output" build "${source}" -o "${preserved}" -o "${preserved}")
reject("missing target" build --emit-obj-selfhost vpp_cycle "${source}" "${preserved}")
reject("invalid target" build --target aarch64-plan9 --emit-obj-selfhost vpp_cycle "${source}" "${preserved}")
reject("missing function" build --target aarch64-apple-darwin --emit-obj-selfhost absent "${source}" "${preserved}")
reject("conflicting modes" build --target aarch64-apple-darwin --emit-obj-selfhost vpp_cycle
    --bytecode "${source}" "${preserved}")
reject("extra input" build "${source}" "${source}" -o "${preserved}")
file(WRITE "${VPP_OUTPUT_DIR}/invalid.vi" "hàm bad(a) { trả về a && 1; };\n")
reject("unsupported source" build --target aarch64-apple-darwin --emit-obj-selfhost bad
    "${VPP_OUTPUT_DIR}/invalid.vi" "${preserved}")
reject("unlinked raw CALL" build --target aarch64-apple-darwin --emit-raw chua_ho_tro
    "${leaf}" "${preserved}")
reject("unknown assembly function" build --target aarch64-apple-darwin --emit-asm missing
    "${leaf}" "${preserved}")

# Host identity checks must still reject direct, symbolic and hard-link aliases
# of the input before the V++ file writer is called.
file(SHA256 "${source}" source_hash)
foreach(alias IN ITEMS direct symbolic hard)
    set(destination "${source}")
    if (NOT alias STREQUAL "direct")
        set(destination "${VPP_OUTPUT_DIR}/alias-${alias}.vi")
        file(REMOVE "${destination}")
        if (alias STREQUAL "symbolic")
            file(CREATE_LINK "${source}" "${destination}" SYMBOLIC RESULT link_result)
        else()
            file(CREATE_LINK "${source}" "${destination}" RESULT link_result)
        endif()
        if (NOT link_result STREQUAL "0")
            message(FATAL_ERROR "Cannot prepare input alias: ${link_result}")
        endif()
    endif()
    execute_process(COMMAND ${native_command} build --target aarch64-apple-darwin
        --emit-obj-selfhost vpp_cycle "${source}" "${destination}"
        RESULT_VARIABLE status OUTPUT_VARIABLE output ERROR_VARIABLE errors TIMEOUT 30)
    file(SHA256 "${source}" after_hash)
    if (status STREQUAL "0" OR NOT source_hash STREQUAL after_hash)
        message(FATAL_ERROR "CLI overwrote ${alias} input alias: ${output} ${errors}")
    endif()
endforeach()
message(STATUS "Native V++ CLI: ${checked}/${checked} object parity, no Stage 0 subprocess, argument errors and input preservation PASS")
