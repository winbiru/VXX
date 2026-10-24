# Use the prepared V++/ASM native compiler for standalone runtime fixtures.
# Stage 0 and native Stage 1 compile `in expression;` directly.
function(vpp_native_compile_command source executable result)
    if (DEFINED VPP_STAGE1 AND NOT "${VPP_STAGE1}" STREQUAL "")
        if (NOT EXISTS "${VPP_STAGE1}")
            message(FATAL_ERROR "native Stage 1 compiler missing: ${VPP_STAGE1}")
        endif()
        set(${result} "${VPP_STAGE1}" build "${source}" -o "${executable}"
            PARENT_SCOPE)
    else()
        set(${result} "${VPP_EXEC}" build --target "${VPP_TARGET}"
            --emit-exe-selfhost "${source}" "${executable}" PARENT_SCOPE)
    endif()
endfunction()
