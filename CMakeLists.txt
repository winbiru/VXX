cmake_minimum_required(VERSION 3.15)
project(VPP VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

option(BUILD_TESTS "Build V++ .vi regression tests" ON)
option(BUILD_BENCHMARKS "Build repeatable developer benchmarks" OFF)
option(BUILD_SHARED_LIBS "Build shared libraries instead of static" OFF)
# Standalone Windows executables must also run on machines without VC Redist.
# Apply to every target so libraries and the CLI use the same CRT allocator.
if (MSVC AND NOT BUILD_SHARED_LIBS AND NOT DEFINED CMAKE_MSVC_RUNTIME_LIBRARY)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()
option(VPP_ENABLE_COVERAGE "Enable GCC/Clang coverage instrumentation" OFF)
option(VPP_ENABLE_CLANG_TIDY "Run clang-tidy while compiling C++ targets" OFF)

if (VPP_ENABLE_COVERAGE)
    if (MSVC)
        message(FATAL_ERROR "VPP_ENABLE_COVERAGE currently requires GCC or Clang")
    endif()
    if (NOT CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        message(FATAL_ERROR "Unsupported compiler for coverage: ${CMAKE_CXX_COMPILER_ID}")
    endif()
    add_compile_options(-O0 -g --coverage)
    # GCC's gcov counters may be updated by multiple processes during the
    # integration suite. Atomic profile updates prevent corrupted/negative
    # execution counts when those processes write the same .gcda files.
    if (CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        add_compile_options(-fprofile-update=atomic)
    endif()
    add_link_options(--coverage)
endif()

if (VPP_ENABLE_CLANG_TIDY)
    find_program(VPP_CLANG_TIDY_EXECUTABLE NAMES clang-tidy)
    if (NOT VPP_CLANG_TIDY_EXECUTABLE)
        message(FATAL_ERROR "VPP_ENABLE_CLANG_TIDY=ON but clang-tidy was not found")
    endif()
    set(CMAKE_CXX_CLANG_TIDY
        "${VPP_CLANG_TIDY_EXECUTABLE};--config-file=${CMAKE_SOURCE_DIR}/.clang-tidy")
endif()

find_package(Threads REQUIRED)
if (UNIX AND NOT APPLE)
    # Release Linux should not require libcrypto.so to already exist on the
    # user's machine. Ubuntu's libssl-dev provides the static archive.
    set(OPENSSL_USE_STATIC_LIBS TRUE)
    find_package(OpenSSL REQUIRED)
endif()

# Public include dir for headers
set(VPP_INCLUDE_DIR ${CMAKE_SOURCE_DIR}/src/include)

# Output directories for libs and binaries
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# Explicit ownership keeps target dependencies directional; no target is
# allowed to use the old catch-all `helpers` grouping.
set(VPP_CORE_SOURCES
    ${CMAKE_SOURCE_DIR}/src/core/text.cpp
    ${CMAKE_SOURCE_DIR}/src/core/semver.cpp
    ${CMAKE_SOURCE_DIR}/src/core/package_manifest.cpp
    ${CMAKE_SOURCE_DIR}/src/core/package_source.cpp
    ${CMAKE_SOURCE_DIR}/src/core/package_solver.cpp
    ${CMAKE_SOURCE_DIR}/src/core/package_installer.cpp
    ${CMAKE_SOURCE_DIR}/src/core/package_cache.cpp)
set(VPP_FRONTEND_SOURCES
    ${CMAKE_SOURCE_DIR}/src/frontend/keywords.cpp
    ${CMAKE_SOURCE_DIR}/src/frontend/lexer.cpp
    ${CMAKE_SOURCE_DIR}/src/frontend/ast.cpp
    ${CMAKE_SOURCE_DIR}/src/frontend/parser.cpp)
set(VPP_BYTECODE_SOURCES
    ${CMAKE_SOURCE_DIR}/src/bytecode/opcode.cpp
    ${CMAKE_SOURCE_DIR}/src/bytecode/verifier.cpp)
set(VPP_COMPILER_SOURCES
    ${CMAKE_SOURCE_DIR}/src/compiler/support/expression.cpp
    ${CMAKE_SOURCE_DIR}/src/compiler/support/storeString.cpp
    ${CMAKE_SOURCE_DIR}/src/compiler/support/symbolTable.cpp
    ${CMAKE_SOURCE_DIR}/src/compiler/support/token_utils.cpp
    ${CMAKE_SOURCE_DIR}/src/compiler/module_graph.cpp
    ${CMAKE_SOURCE_DIR}/src/compiler/package_resolver.cpp
    ${CMAKE_SOURCE_DIR}/src/compiler/semantic.cpp
    ${CMAKE_SOURCE_DIR}/src/compiler/ir.cpp
    ${CMAKE_SOURCE_DIR}/src/compiler/optimizer.cpp
    ${CMAKE_SOURCE_DIR}/src/compiler/codegen.cpp
    ${CMAKE_SOURCE_DIR}/src/compiler/compileRegistry.cpp
    ${CMAKE_SOURCE_DIR}/src/compiler/compiler.cpp)
set(VPP_RUNTIME_SOURCES
    ${CMAKE_SOURCE_DIR}/src/runtime/heap.cpp
    ${CMAKE_SOURCE_DIR}/src/runtime/module.cpp
    ${CMAKE_SOURCE_DIR}/src/runtime/object.cpp
    ${CMAKE_SOURCE_DIR}/src/runtime/vm.cpp
    ${CMAKE_SOURCE_DIR}/src/runtime/native/vm_native_collection_helpers.cpp
    ${CMAKE_SOURCE_DIR}/src/runtime/native/vm_native_compiler_helpers.cpp
    ${CMAKE_SOURCE_DIR}/src/runtime/native/vm_native_extended_helpers.cpp
    ${CMAKE_SOURCE_DIR}/src/runtime/native/vm_native_m3_helpers.cpp
    ${CMAKE_SOURCE_DIR}/src/runtime/native/vm_native_format_helpers.cpp
    ${CMAKE_SOURCE_DIR}/src/runtime/native/vm_native_helpers.cpp
    ${CMAKE_SOURCE_DIR}/src/runtime/native/vm_native_stdlib_helpers.cpp)
set(VPP_TOOLING_SOURCES
    ${CMAKE_SOURCE_DIR}/src/tooling/tooling.cpp)
set(VPP_CLI_SOURCES ${CMAKE_SOURCE_DIR}/src/cli/main.cpp)
set(VDK_SOURCES ${CMAKE_SOURCE_DIR}/src/vdk/main.cpp)

if (WIN32)
    set(VDK_PLATFORM "windows")
elseif(APPLE)
    set(VDK_PLATFORM "macos")
elseif(UNIX)
    set(VDK_PLATFORM "linux")
else()
    set(VDK_PLATFORM "unknown")
endif()

string(TOLOWER "${CMAKE_SYSTEM_PROCESSOR}" VDK_PROCESSOR_LOWER)
if (VDK_PROCESSOR_LOWER MATCHES "^(x86_64|amd64)$")
    set(VDK_ARCH "x64")
elseif(VDK_PROCESSOR_LOWER MATCHES "^(aarch64|arm64)$")
    set(VDK_ARCH "arm64")
elseif(VDK_PROCESSOR_LOWER MATCHES "^(i[3-6]86|x86)$")
    set(VDK_ARCH "x86")
else()
    set(VDK_ARCH "${VDK_PROCESSOR_LOWER}")
endif()

configure_file(
    ${CMAKE_SOURCE_DIR}/cmake/vdk-release.json.in
    ${CMAKE_BINARY_DIR}/vdk-release.json
    @ONLY)

# Create library helper (accepts sources passed directly)
function(vpp_add_module name)
    set(sources ${ARGN})
    if (NOT sources)
        message(FATAL_ERROR "No sources provided for module ${name}")
    endif()
    if (BUILD_SHARED_LIBS)
        add_library(${name} SHARED ${sources})
    else()
        add_library(${name} STATIC ${sources})
    endif()
    target_include_directories(${name} PUBLIC ${VPP_INCLUDE_DIR})
    target_compile_features(${name} PUBLIC cxx_std_17)
    if (MSVC)
        target_compile_definitions(${name} PRIVATE
            _CRT_SECURE_NO_WARNINGS
            _CRT_NONSTDC_NO_WARNINGS
            popen=_popen
            pclose=_pclose
        )
        # Source and bundled module paths contain Vietnamese UTF-8 text.
        target_compile_options(${name} PRIVATE /W4 /permissive- /utf-8)
    else()
        target_compile_options(${name} PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endfunction()

# The dependency graph is intentionally one-way:
# core -> bytecode -> frontend -> compiler; runtime consumes bytecode and
# receives function metadata from the CLI instead of reading compiler globals.
# Tooling depends on compiler; CLI composes runtime and tooling.
vpp_add_module(vpp-core ${VPP_CORE_SOURCES})

vpp_add_module(vpp-frontend ${VPP_FRONTEND_SOURCES})

vpp_add_module(vpp-bytecode ${VPP_BYTECODE_SOURCES})
target_link_libraries(vpp-bytecode PUBLIC vpp-core)
target_link_libraries(vpp-frontend PUBLIC vpp-bytecode vpp-core)

vpp_add_module(vpp-compiler ${VPP_COMPILER_SOURCES})
target_link_libraries(vpp-compiler PUBLIC vpp-frontend vpp-bytecode vpp-core)

vpp_add_module(vpp-runtime ${VPP_RUNTIME_SOURCES})
target_link_libraries(vpp-runtime PUBLIC vpp-bytecode vpp-core vpp-compiler Threads::Threads)
if (WIN32)
    target_link_libraries(vpp-runtime PUBLIC ws2_32 bcrypt secur32 crypt32)
elseif(APPLE)
    find_library(VPP_SECURITY_FRAMEWORK Security REQUIRED)
    find_library(VPP_COREFOUNDATION_FRAMEWORK CoreFoundation REQUIRED)
    target_link_libraries(
        vpp-runtime PUBLIC
        ${VPP_SECURITY_FRAMEWORK}
        ${VPP_COREFOUNDATION_FRAMEWORK})
elseif(UNIX)
    target_link_libraries(vpp-runtime PUBLIC OpenSSL::SSL OpenSSL::Crypto)
endif()

vpp_add_module(vpp-tooling ${VPP_TOOLING_SOURCES})
target_link_libraries(vpp-tooling PUBLIC vpp-compiler vpp-bytecode)

add_library(vpp::core ALIAS vpp-core)
add_library(vpp::bytecode ALIAS vpp-bytecode)
add_library(vpp::frontend ALIAS vpp-frontend)
add_library(vpp::compiler ALIAS vpp-compiler)
add_library(vpp::runtime ALIAS vpp-runtime)
add_library(vpp::tooling ALIAS vpp-tooling)

# CLI executable composes runtime and developer tooling.
add_executable(vpp-cli ${VPP_CLI_SOURCES})
target_link_libraries(vpp-cli PRIVATE vpp-runtime vpp-tooling)
target_include_directories(vpp-cli PRIVATE ${VPP_INCLUDE_DIR})
target_compile_definitions(vpp-cli PRIVATE VPP_VERSION_STRING="${PROJECT_VERSION}")

add_executable(vdk ${VDK_SOURCES})
target_compile_features(vdk PRIVATE cxx_std_17)
target_compile_definitions(vdk PRIVATE VDK_VERSION_STRING="${PROJECT_VERSION}")
if (MSVC)
    target_compile_options(vdk PRIVATE /W4 /permissive- /utf-8)
else()
    target_compile_options(vdk PRIVATE -Wall -Wextra -Wpedantic)
endif()

if (MSVC)
    target_compile_definitions(vpp-cli PRIVATE
        _CRT_SECURE_NO_WARNINGS
        _CRT_NONSTDC_NO_WARNINGS
        popen=_popen
        pclose=_pclose
    )
    target_compile_options(vpp-cli PRIVATE /W4 /permissive- /utf-8)
else()
    target_compile_options(vpp-cli PRIVATE -Wall -Wextra -Wpedantic)
endif()

# Backward-compat: keep legacy binary name available for existing scripts.
add_custom_command(TARGET vpp-cli POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:vpp-cli>
            ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/vietvm-cli)
add_custom_command(TARGET vpp-cli POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:vpp-cli>
            ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/vpp${CMAKE_EXECUTABLE_SUFFIX})

# Regression tests are V++ .vi programs under src/tests/.
if (BUILD_TESTS)
    enable_testing()

    # Focused internal RC gate. The old broad C++ unit tree was removed on
    # purpose; keep only invariants that cannot be exercised from public .vi
    # source: malformed AST/IR rejection and bytecode verifier metadata checks.
    add_executable(vpp-rc-internal-hardening
        ${CMAKE_SOURCE_DIR}/scripts/quality/rc_internal_hardening.cpp)
    target_link_libraries(vpp-rc-internal-hardening PRIVATE vpp-compiler)
    target_include_directories(vpp-rc-internal-hardening PRIVATE ${VPP_INCLUDE_DIR})
    target_compile_features(vpp-rc-internal-hardening PRIVATE cxx_std_17)
    if (MSVC)
        target_compile_options(vpp-rc-internal-hardening PRIVATE /W4 /permissive- /utf-8)
    else()
        target_compile_options(vpp-rc-internal-hardening PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    add_test(NAME vpp-rc-internal-hardening COMMAND vpp-rc-internal-hardening)

    add_executable(vpp-runtime-p0-hardening
        ${CMAKE_SOURCE_DIR}/scripts/quality/runtime_p0_hardening.cpp)
    target_link_libraries(vpp-runtime-p0-hardening PRIVATE vpp-runtime)
    target_include_directories(vpp-runtime-p0-hardening PRIVATE ${VPP_INCLUDE_DIR})
    target_compile_features(vpp-runtime-p0-hardening PRIVATE cxx_std_17)
    if (MSVC)
        target_compile_options(vpp-runtime-p0-hardening PRIVATE /W4 /permissive- /utf-8)
    else()
        target_compile_options(vpp-runtime-p0-hardening PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    add_test(NAME vpp-runtime-p0-hardening COMMAND vpp-runtime-p0-hardening)

    add_executable(vpp-import-precedence-hardening
        ${CMAKE_SOURCE_DIR}/scripts/quality/import_precedence_hardening.cpp)
    target_link_libraries(vpp-import-precedence-hardening PRIVATE vpp-compiler)
    target_include_directories(vpp-import-precedence-hardening PRIVATE ${VPP_INCLUDE_DIR})
    target_compile_features(vpp-import-precedence-hardening PRIVATE cxx_std_17)
    if (MSVC)
        target_compile_options(vpp-import-precedence-hardening PRIVATE /W4 /permissive- /utf-8)
    else()
        target_compile_options(vpp-import-precedence-hardening PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    add_test(NAME vpp-import-precedence-hardening COMMAND vpp-import-precedence-hardening)

    if (UNIX AND EXISTS ${CMAKE_SOURCE_DIR}/run_tests.sh)
        add_test(NAME vpp-integration
                 COMMAND ${CMAKE_COMMAND} -E env
                         "VPP_EXEC=$<TARGET_FILE:vpp-cli>"
                         ${CMAKE_SOURCE_DIR}/run_tests.sh)
        set_tests_properties(vpp-integration PROPERTIES
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
    elseif (WIN32 AND EXISTS ${CMAKE_SOURCE_DIR}/scripts/windows/run-tests.ps1)
        # The bundled module names are UTF-8 Vietnamese text.  PowerShell 7
        # reads UTF-8 scripts consistently, unlike legacy Windows PowerShell.
        find_program(VPP_POWERSHELL_EXECUTABLE NAMES pwsh)
        if (VPP_POWERSHELL_EXECUTABLE)
            add_test(NAME vpp-integration
                     COMMAND ${VPP_POWERSHELL_EXECUTABLE}
                             -NoLogo -NoProfile -NonInteractive
                             -ExecutionPolicy Bypass
                             -File ${CMAKE_SOURCE_DIR}/scripts/windows/run-tests.ps1
                             -VppExecutable $<TARGET_FILE:vpp-cli>)
            set_tests_properties(vpp-integration PROPERTIES
                WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
        else()
            message(WARNING "PowerShell 7 (pwsh) was not found; Windows integration tests are disabled")
        endif()
    endif()
endif()

if (BUILD_BENCHMARKS)
    add_subdirectory(benchmark)
endif()

# Install rules (optional)
install(TARGETS vdk vpp-cli vpp-core vpp-frontend vpp-bytecode vpp-compiler vpp-runtime vpp-tooling
        RUNTIME DESTINATION bin
        ARCHIVE DESTINATION lib
        LIBRARY DESTINATION lib)
install(PROGRAMS $<TARGET_FILE:vpp-cli>
        DESTINATION bin
        RENAME vpp${CMAKE_EXECUTABLE_SUFFIX})
install(FILES
        ${CMAKE_BINARY_DIR}/vdk-release.json
        ${CMAKE_SOURCE_DIR}/vpp-libraries.json
        DESTINATION .)
install(DIRECTORY ${CMAKE_SOURCE_DIR}/gói/
        DESTINATION gói
        PATTERN ".DS_Store" EXCLUDE)
install(DIRECTORY ${CMAKE_SOURCE_DIR}/docs/
        DESTINATION docs
        PATTERN ".DS_Store" EXCLUDE)
install(DIRECTORY ${CMAKE_SOURCE_DIR}/templates/
        DESTINATION templates
        PATTERN ".DS_Store" EXCLUDE)
install(DIRECTORY ${CMAKE_SOURCE_DIR}/examples/
        DESTINATION examples
        PATTERN ".DS_Store" EXCLUDE
        PATTERN ".vpp" EXCLUDE)
install(DIRECTORY ${CMAKE_SOURCE_DIR}/src/include/
        DESTINATION include
        PATTERN ".DS_Store" EXCLUDE)
install(FILES ${CMAKE_SOURCE_DIR}/LICENSE DESTINATION licenses)

set(CPACK_PACKAGE_NAME "VDK")
set(CPACK_PACKAGE_VENDOR "V++")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_FILE_NAME "vdk-${PROJECT_VERSION}-${VDK_PLATFORM}-${VDK_ARCH}")
if (WIN32)
    set(CPACK_GENERATOR "ZIP")
else()
    set(CPACK_GENERATOR "TGZ")
endif()
include(CPack)

# Developer notes:
# - Keep source ownership explicit; do not add new files to a generic helpers target.
# - Move public headers to src/include/vpp/<module>/ as modules stabilize.
# - Keep dependencies pointing toward lower-level modules only.
