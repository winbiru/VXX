foreach(required IN ITEMS VPP_CLI VPP_NATIVE_COMPILER VPP_ROOT VPP_OUTPUT_DIR)
    if (NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "Native Unicode CLI missing ${required}")
    endif()
endforeach()

# Include a character outside legacy Western code pages, spaces and a Unicode
# function name. Both direct native argv and the compatibility CLI must survive.
set(directory "${VPP_OUTPUT_DIR}/thư mục Ω 🧪")
file(MAKE_DIRECTORY "${directory}")
set(source "${directory}/nguồn thêm.vi")
configure_file("${VPP_ROOT}/scripts/test/kiem_tra_native_calls.vi" "${source}" COPYONLY)
set(environment "${CMAKE_COMMAND}" -E env
    --unset=VPP_HOME --unset=VPP_AOT_ROOT --unset=VPP_AOT_LIB_ROOT
    "PATH=" "VPP_NATIVE_COMPILER=${VPP_NATIVE_COMPILER}")
foreach(mode IN ITEMS direct delegated)
    set(compiler "${VPP_NATIVE_COMPILER}")
    if (mode STREQUAL "delegated")
        set(compiler "${VPP_CLI}")
    endif()
    set(object "${directory}/${mode}-thêm.obj")
    execute_process(COMMAND ${environment} "${compiler}"
        build --target x86_64-windows-msvc --emit-obj thêm "${source}" "${object}"
        RESULT_VARIABLE status OUTPUT_VARIABLE output ERROR_VARIABLE errors TIMEOUT 45)
    if (NOT status STREQUAL "0" OR NOT EXISTS "${object}" OR NOT output MATCHES "thêm")
        message(FATAL_ERROR "Native Unicode CLI ${mode} failed (${status}):\n${output}\n${errors}")
    endif()
    file(READ "${object}" machine LIMIT 2 HEX)
    if (NOT machine STREQUAL "6486")
        message(FATAL_ERROR "Native Unicode CLI ${mode} did not emit AMD64 COFF")
    endif()
    file(SHA256 "${object}" hash)
    if (mode STREQUAL "direct")
        set(direct_hash "${hash}")
    elseif (NOT hash STREQUAL direct_hash)
        message(FATAL_ERROR "Native Unicode CLI delegation changed object bytes")
    endif()
endforeach()
message(STATUS "Native Unicode CLI: direct/delegated UTF-8 arguments and COFF parity PASS")
