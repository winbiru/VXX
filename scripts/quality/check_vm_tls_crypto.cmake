set(tls_image "${VPP_OUTPUT_DIR}/tls-crypto.vmb")
execute_process(COMMAND "${VPP_NATIVE_COMPILER}" vm-build
    "${VPP_ROOT}/src/tests/fixtures/vm_tls_crypto.vi" -o "${tls_image}"
    RESULT_VARIABLE status OUTPUT_VARIABLE output ERROR_VARIABLE errors TIMEOUT 180)
if(NOT status STREQUAL "0")
    message(FATAL_ERROR "V++ TLS crypto compile: ${status}\n${output}\n${errors}")
endif()
execute_process(COMMAND "${CMAKE_COMMAND}" -E env "PATH=" "${VPP_VM}" "${tls_image}"
    RESULT_VARIABLE status OUTPUT_VARIABLE output ERROR_VARIABLE errors TIMEOUT 240)
if(NOT status STREQUAL "0" OR NOT output STREQUAL "TLS VPP RFC VECTORS OK\n")
    message(FATAL_ERROR "V++ TLS crypto vectors/negative authentication: ${status}\n${output}\n${errors}")
endif()
message(STATUS "V++ HKDF-SHA256, ChaCha20, Poly1305 and AEAD RFC vectors, bad tags/parameters: PASS")
