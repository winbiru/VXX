cmake_minimum_required(VERSION 3.20)
project(vpp_installed_native_smoke LANGUAGES CXX)

# Exercise only the installed compiler and stdlib, using the host linker to
# execute real machine objects emitted from .vi functions.
foreach(required IN ITEMS VPP_INSTALL_ROOT VPP_SOURCE_ROOT VPP_TARGET)
    if (NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "Missing -D${required}")
    endif()
endforeach()

if (NOT DEFINED VPP_EXEC)
    set(VPP_EXEC "${VPP_INSTALL_ROOT}/bin/vpp${CMAKE_EXECUTABLE_SUFFIX}")
endif()
if (NOT DEFINED VPP_CLI)
    set(VPP_CLI "${VPP_INSTALL_ROOT}/bin/vpp-cli${CMAKE_EXECUTABLE_SUFFIX}")
endif()
foreach(required_file IN ITEMS
        "${VPP_EXEC}" "${VPP_CLI}"
        "${VPP_INSTALL_ROOT}/gói/chuẩn/chính.vi"
        "${VPP_SOURCE_ROOT}/src/tests/kiem_tra_native_calls.vi"
        "${VPP_SOURCE_ROOT}/scripts/quality/native_calls_smoke.c")
    if (NOT EXISTS "${required_file}")
        message(FATAL_ERROR "Installation or smoke fixture is missing: ${required_file}")
    endif()
endforeach()

enable_testing()
# Keep the fixture outside the source checkout so package resolution must
# discover the stdlib through VPP_HOME in the installation under test.
configure_file("${VPP_SOURCE_ROOT}/src/tests/kiem_tra_cai_dat_cicd.vi"
    "${CMAKE_CURRENT_BINARY_DIR}/installed-stdlib.vi" COPYONLY)
add_test(NAME installed-stdlib-vi
    COMMAND "${CMAKE_COMMAND}" -E env "VPP_HOME=${VPP_INSTALL_ROOT}"
        "${VPP_EXEC}" chạy "${CMAKE_CURRENT_BINARY_DIR}/installed-stdlib.vi")
set_tests_properties(installed-stdlib-vi PROPERTIES
    PASS_REGULAR_EXPRESSION "vpp_installed_stdlib_ok" TIMEOUT 90)

set(source "${VPP_SOURCE_ROOT}/src/tests/kiem_tra_native_calls.vi")
set(object_dir "${CMAKE_CURRENT_BINARY_DIR}/native-objects")
file(MAKE_DIRECTORY "${object_dir}")
if (WIN32)
    set(object_suffix ".obj")
else()
    set(object_suffix ".o")
endif()

set(objects)
foreach(func IN ITEMS co_so them ten_ham_dai nhan_sau ket_hop tinh_vong
                      bon goi_bon tong_muoi goi_muoi)
    set(object "${object_dir}/${func}${object_suffix}")
    add_custom_command(OUTPUT "${object}"
        COMMAND "${CMAKE_COMMAND}" -E env "VPP_HOME=${VPP_INSTALL_ROOT}"
            "${VPP_CLI}" build --target "${VPP_TARGET}" --emit-obj
            "${func}" "${source}" "${object}"
        DEPENDS "${VPP_CLI}" "${source}"
        COMMENT "Compile V++ ${func} for ${VPP_TARGET} with the installed compiler"
        VERBATIM)
    set_source_files_properties("${object}" PROPERTIES
        GENERATED TRUE EXTERNAL_OBJECT TRUE)
    list(APPEND objects "${object}")
endforeach()

# The existing C ABI harness is compiled as C++ for Visual Studio projects.
# Its extern "C" declarations ensure linkage to the V++ native symbols.
set(harness "${VPP_SOURCE_ROOT}/scripts/quality/native_calls_smoke.c")
set_source_files_properties("${harness}" PROPERTIES LANGUAGE CXX)
add_executable(vpp-installed-native-calls "${harness}" ${objects})
add_test(NAME installed-native-objects COMMAND $<TARGET_FILE:vpp-installed-native-calls>)
set_tests_properties(installed-native-objects PROPERTIES
    PASS_REGULAR_EXPRESSION "native_calls_ok" TIMEOUT 60)
