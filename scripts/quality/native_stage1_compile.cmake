# Use the prepared V++/ASM native compiler for standalone runtime fixtures.
# The full frontend's `in expression;` syntax has a separate Stage 0 oracle;
# Stage 1's i32 runtime exposes the same print operation as `in_số(expr);`.
# Transform only the temporary compilation copy, keeping the original source
# available to test the full frontend on targets without a native Stage 1.
function(vpp_native_compile_command source executable result)
    if (DEFINED VPP_STAGE1 AND NOT "${VPP_STAGE1}" STREQUAL "")
        if (NOT EXISTS "${VPP_STAGE1}")
            message(FATAL_ERROR "native Stage 1 compiler missing: ${VPP_STAGE1}")
        endif()
        file(READ "${source}" native_contents)
        set(original_contents "${native_contents}")
        string(REGEX REPLACE "(^|\n)([ \t]*)in ([^;\n]*);"
            "\\1\\2in_số(\\3);" native_contents "${native_contents}")
        set(native_source "${source}")
        if (NOT native_contents STREQUAL original_contents)
            get_filename_component(source_name "${source}" NAME)
            set(native_source "${VPP_OUTPUT_DIR}/${source_name}.stage1.vi")
            file(WRITE "${native_source}" "${native_contents}")
        endif()
        set(${result} "${VPP_STAGE1}" build "${native_source}" -o "${executable}"
            PARENT_SCOPE)
    else()
        set(${result} "${VPP_EXEC}" build --target "${VPP_TARGET}"
            --emit-exe-selfhost "${source}" "${executable}" PARENT_SCOPE)
    endif()
endfunction()
