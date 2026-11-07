# Take addresses of every ABI entry called by the C++ Stage 0 dispatcher.
# This makes missing or duplicate definitions fail at link time, even when
# the runtime probe does not execute a particular primitive.
file(STRINGS "${VPP_ROOT}/src/native/aot_native_core.cpp" declarations
    REGEX "^std::int32_t vpp_native_[a-z0-9_]+\\(")
if(NOT declarations)
    message(FATAL_ERROR "No Stage 0 native ABI declarations found")
endif()
file(WRITE "${VPP_STAGE0_SYMBOLS}" "#include <stdint.h>\n")
set(references "")
foreach(declaration IN LISTS declarations)
    string(REPLACE "std::int32_t" "int32_t" declaration "${declaration}")
    string(REGEX REPLACE ";$" "" declaration "${declaration}")
    file(APPEND "${VPP_STAGE0_SYMBOLS}" "extern ${declaration};\n")
    string(REGEX MATCH "vpp_native_[a-z0-9_]+" symbol "${declaration}")
    string(APPEND references "    (void (*)(void))${symbol},\n")
endforeach()
file(APPEND "${VPP_STAGE0_SYMBOLS}"
    "static void (*volatile references[])(void) = {\n${references}};\n"
    "int vpp_windows_stage0_abi(void) {\n"
    "    for (unsigned i = 0; i < sizeof references / sizeof references[0]; ++i)\n"
    "        if (!references[i]) return -1;\n"
    "    return 0;\n}\n")
