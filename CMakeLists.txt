cmake_minimum_required(VERSION 3.15)
project(VPP VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

option(BUILD_TESTS "Build V++ .vi regression tests" ON)
option(VPP_TEST_DNS_LOOPBACK "Enable DNS TCP fallback loopback integration test" OFF)
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
    # Keep counters and debug info while reducing instrumented frontend and
    # runtime overhead. Native compiler self-hosting is verified separately
    # against machine-code executables, outside the coverage build.
    add_compile_options(-O1 -g --coverage)
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
find_package(PkgConfig QUIET)
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
    ${CMAKE_SOURCE_DIR}/src/native/target.cpp
    ${CMAKE_SOURCE_DIR}/src/native/raw_codegen.cpp
    ${CMAKE_SOURCE_DIR}/src/native/object_writer.cpp
    ${CMAKE_SOURCE_DIR}/src/native/x86_64_codegen.cpp
    ${CMAKE_SOURCE_DIR}/src/native/aarch64_codegen.cpp
    ${CMAKE_SOURCE_DIR}/src/native/riscv64_codegen.cpp
    ${CMAKE_SOURCE_DIR}/src/native/armv7_codegen.cpp
    ${CMAKE_SOURCE_DIR}/src/native/i386_codegen.cpp
    ${CMAKE_SOURCE_DIR}/src/native/powerpc_codegen.cpp
    ${CMAKE_SOURCE_DIR}/src/native/powerpc64_codegen.cpp
    ${CMAKE_SOURCE_DIR}/src/native/powerpc64le_codegen.cpp
    ${CMAKE_SOURCE_DIR}/src/native/mips_codegen.cpp
    ${CMAKE_SOURCE_DIR}/src/native/mipsel_codegen.cpp
    ${CMAKE_SOURCE_DIR}/src/native/mips64_codegen.cpp
    ${CMAKE_SOURCE_DIR}/src/native/mips64el_codegen.cpp
    ${CMAKE_SOURCE_DIR}/src/native/loongarch64_codegen.cpp
    ${CMAKE_SOURCE_DIR}/src/native/s390x_codegen.cpp
    ${CMAKE_SOURCE_DIR}/src/native/sparc_codegen.cpp
    ${CMAKE_SOURCE_DIR}/src/native/sparc64_codegen.cpp
    ${CMAKE_SOURCE_DIR}/src/native/aarch64_jit.cpp
    ${CMAKE_SOURCE_DIR}/src/native/x64_jit.cpp
    ${CMAKE_SOURCE_DIR}/src/runtime/heap.cpp
    ${CMAKE_SOURCE_DIR}/src/runtime/foreign.cpp
    ${CMAKE_SOURCE_DIR}/src/runtime/file_machine.cpp
    ${CMAKE_SOURCE_DIR}/src/runtime/socket_machine.cpp
    ${CMAKE_SOURCE_DIR}/src/runtime/file_runtime.cpp
    ${CMAKE_SOURCE_DIR}/src/runtime/process_identity.cpp
    ${CMAKE_SOURCE_DIR}/src/runtime/time_sleep.cpp
    ${CMAKE_SOURCE_DIR}/src/runtime/time_clock.cpp
    ${CMAKE_SOURCE_DIR}/src/runtime/entropy_machine.cpp
    ${CMAKE_SOURCE_DIR}/src/runtime/environment_machine.cpp
    ${CMAKE_SOURCE_DIR}/src/runtime/time_local.cpp
    ${CMAKE_SOURCE_DIR}/src/runtime/directory_machine.cpp
    ${CMAKE_SOURCE_DIR}/src/runtime/path_machine.cpp
    ${CMAKE_SOURCE_DIR}/src/runtime/module.cpp
    ${CMAKE_SOURCE_DIR}/src/runtime/object.cpp
    ${CMAKE_SOURCE_DIR}/src/runtime/literal.cpp
    ${CMAKE_SOURCE_DIR}/src/runtime/native/vm_native_collection_helpers.cpp
    ${CMAKE_SOURCE_DIR}/src/runtime/native/vm_native_m3_helpers.cpp
    ${CMAKE_SOURCE_DIR}/src/runtime/native/vm_native_helpers.cpp
    ${CMAKE_SOURCE_DIR}/src/runtime/native/vm_native_stdlib_helpers.cpp)
set(VPP_VM_RUNTIME_SOURCES
    ${CMAKE_SOURCE_DIR}/src/runtime/vm.cpp
    ${CMAKE_SOURCE_DIR}/src/runtime/native/vm_native_compiler_helpers.cpp)
set(VPP_TOOLING_SOURCES
    ${CMAKE_SOURCE_DIR}/src/tooling/tooling.cpp)
set(VPP_CLI_SOURCES
    ${CMAKE_SOURCE_DIR}/src/cli/main.cpp
    ${CMAKE_SOURCE_DIR}/src/cli/native_vpp_codegen.cpp)
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

vpp_add_module(vpp-runtime-native ${VPP_RUNTIME_SOURCES})
if (UNIX AND CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|amd64|aarch64|arm64)$")
    enable_language(ASM)
    target_sources(vpp-runtime-native PRIVATE scripts/ASM/vpp_file_primitives.S)
    target_sources(vpp-runtime-native PRIVATE scripts/ASM/vpp_environment_primitives.S)
    target_sources(vpp-runtime-native PRIVATE scripts/ASM/vpp_localtime_primitives.S)
    target_sources(vpp-runtime-native PRIVATE scripts/ASM/vpp_directory_primitives.S)
    target_sources(vpp-runtime-native PRIVATE scripts/ASM/vpp_path_primitives.S)
    if(APPLE)
        target_sources(vpp-runtime-native PRIVATE scripts/ASM/vpp_clock_primitives.S)
        target_sources(vpp-runtime-native PRIVATE scripts/ASM/vpp_entropy_primitives.S)
    endif()
    target_compile_definitions(vpp-runtime-native PRIVATE VPP_FILE_MACHINE_ASM=1)
endif()
target_link_libraries(vpp-runtime-native PUBLIC vpp-bytecode vpp-core Threads::Threads)
vpp_add_module(vpp-runtime ${VPP_VM_RUNTIME_SOURCES})
target_link_libraries(vpp-runtime PUBLIC vpp-runtime-native vpp-compiler)
option(VPP_STATIC_LIBFFI "Link libffi statically for standalone release archives" OFF)
option(VPP_ENABLE_LEGACY_LIBFFI
    "Enable legacy FFI bytecode compatibility via libffi (Machine ABI remains available without it)" ON)
if (VPP_ENABLE_LEGACY_LIBFFI AND PkgConfig_FOUND)
    pkg_check_modules(VPP_LIBFFI QUIET IMPORTED_TARGET libffi)
endif()
if (VPP_ENABLE_LEGACY_LIBFFI AND VPP_LIBFFI_FOUND)
    if (VPP_STATIC_LIBFFI AND UNIX)
        # pkg-config's imported target normally selects libffi.so/.dylib.
        # A copied release binary cannot rely on a Homebrew/Linux runtime
        # installation, so require the distribution's actual static archive.
        find_file(VPP_LIBFFI_STATIC_ARCHIVE NAMES libffi.a
            PATHS ${VPP_LIBFFI_LIBRARY_DIRS} NO_DEFAULT_PATH)
        if (NOT VPP_LIBFFI_STATIC_ARCHIVE)
            message(FATAL_ERROR
                "Standalone V++ release requires libffi.a in ${VPP_LIBFFI_LIBRARY_DIRS}")
        endif()
        target_include_directories(vpp-runtime-native PRIVATE ${VPP_LIBFFI_INCLUDE_DIRS})
        target_link_libraries(vpp-runtime-native PUBLIC
            "${VPP_LIBFFI_STATIC_ARCHIVE}" ${CMAKE_DL_LIBS})
    else()
        target_link_libraries(vpp-runtime-native PUBLIC PkgConfig::VPP_LIBFFI ${CMAKE_DL_LIBS})
    endif()
    target_compile_definitions(vpp-runtime-native PRIVATE VPP_HAS_LIBFFI=1)
elseif(UNIX AND VPP_ENABLE_LEGACY_LIBFFI)
    message(FATAL_ERROR "Legacy POSIX FFI requires libffi and pkg-config; set VPP_ENABLE_LEGACY_LIBFFI=OFF to build only Machine ABI paths")
else()
    message(STATUS "Legacy libffi bridge disabled/unavailable; Machine ABI remains enabled")
endif()
if (WIN32)
    target_link_libraries(vpp-runtime-native PUBLIC ws2_32 bcrypt secur32 crypt32)
elseif(APPLE)
    find_library(VPP_SECURITY_FRAMEWORK Security REQUIRED)
    find_library(VPP_COREFOUNDATION_FRAMEWORK CoreFoundation REQUIRED)
    target_link_libraries(
        vpp-runtime-native PUBLIC
        ${VPP_SECURITY_FRAMEWORK}
        ${VPP_COREFOUNDATION_FRAMEWORK})
elseif(UNIX)
    target_link_libraries(vpp-runtime-native PUBLIC OpenSSL::SSL OpenSSL::Crypto)
endif()

vpp_add_module(vpp-tooling ${VPP_TOOLING_SOURCES})
target_link_libraries(vpp-tooling PUBLIC vpp-compiler vpp-bytecode)

vpp_add_module(vpp-native-aot
    ${CMAKE_SOURCE_DIR}/src/native/aot_runtime.cpp
    ${CMAKE_SOURCE_DIR}/src/native/aot_assembly.cpp
    ${CMAKE_SOURCE_DIR}/src/native/aot_driver.cpp)
target_link_libraries(vpp-native-aot PUBLIC vpp-runtime-native)
# The AOT driver starts a separate linker CMake project. Carry the actual
# runtime build's legacy-FFI choice into that project, including installs
# where the original CMakeCache.txt is unavailable.
if (VPP_ENABLE_LEGACY_LIBFFI AND VPP_LIBFFI_FOUND)
    target_compile_definitions(vpp-native-aot PRIVATE VPP_AOT_LINK_LEGACY_LIBFFI=1)
else()
    target_compile_definitions(vpp-native-aot PRIVATE VPP_AOT_LINK_LEGACY_LIBFFI=0)
endif()

add_library(vpp::core ALIAS vpp-core)
add_library(vpp::bytecode ALIAS vpp-bytecode)
add_library(vpp::frontend ALIAS vpp-frontend)
add_library(vpp::compiler ALIAS vpp-compiler)
add_library(vpp::runtime ALIAS vpp-runtime)
add_library(vpp::runtime-native ALIAS vpp-runtime-native)
add_library(vpp::tooling ALIAS vpp-tooling)

# CLI executable composes runtime and developer tooling.
add_executable(vpp-cli ${VPP_CLI_SOURCES})
target_link_libraries(vpp-cli PRIVATE vpp-runtime vpp-tooling vpp-native-aot)
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
    if (UNIX AND NOT VPP_ENABLE_LEGACY_LIBFFI)
        add_test(NAME vpp-machine-no-legacy-libffi
            COMMAND ${CMAKE_COMMAND}
                "-DVPP_CLI=$<TARGET_FILE:vpp-cli>"
                "-DVPP_ROOT=${CMAKE_SOURCE_DIR}"
                -P ${CMAKE_SOURCE_DIR}/scripts/quality/check_machine_no_ffi.cmake)
    endif()
    # Source-level V++ stress cases live alongside the interpreter regressions.
    add_test(NAME vpp-rc-source-corpus
        COMMAND ${CMAKE_COMMAND}
            "-DVPP_EXEC=$<TARGET_FILE:vpp-cli>"
            "-DVPP_ROOT=${CMAKE_SOURCE_DIR}"
            -P ${CMAKE_SOURCE_DIR}/scripts/quality/check_rc_source_corpus.cmake)
    # Legacy compiler bootstrap via the C++ bytecode VM is retired from CTest.
    # Native V++ Stage 1 -> Stage 2 -> Stage 3 is covered by installed smoke
    # on Linux x86-64/AArch64; macOS ARM64 checks native self-hosting via
    # vpp-native-compiler-driver. Ordinary VM feature regressions remain here.

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
    if (VPP_ENABLE_LEGACY_LIBFFI AND VPP_LIBFFI_FOUND AND NOT WIN32)
        target_compile_definitions(
            vpp-runtime-p0-hardening PRIVATE VPP_TEST_POSIX_LIBFFI=1)
        # Let dlsym(RTLD_DEFAULT, ...) resolve the C ABI roundtrip fixtures.
        set_target_properties(vpp-runtime-p0-hardening PROPERTIES ENABLE_EXPORTS ON)
    endif()
    if (MSVC)
        target_compile_options(vpp-runtime-p0-hardening PRIVATE /W4 /permissive- /utf-8)
    else()
        target_compile_options(vpp-runtime-p0-hardening PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    add_test(NAME vpp-runtime-p0-hardening COMMAND vpp-runtime-p0-hardening)

    add_executable(vpp-file-io-hardening scripts/quality/file_io_hardening.cpp)
    target_link_libraries(vpp-file-io-hardening PRIVATE vpp-runtime-native)
    if (MSVC)
        target_compile_options(vpp-file-io-hardening PRIVATE /W4 /utf-8)
    else()
        target_compile_options(vpp-file-io-hardening PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    add_test(NAME vpp-file-io-hardening COMMAND vpp-file-io-hardening)

    add_executable(vpp-process-identity-hardening
        scripts/quality/process_identity_hardening.cpp)
    target_link_libraries(vpp-process-identity-hardening PRIVATE vpp-native-aot vpp-runtime-native)
    if (MSVC)
        target_compile_options(vpp-process-identity-hardening PRIVATE /W4 /utf-8)
    else()
        target_compile_options(vpp-process-identity-hardening PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    add_test(NAME vpp-process-identity-hardening COMMAND vpp-process-identity-hardening)
    add_executable(vpp-time-sleep-hardening scripts/quality/time_sleep_hardening.cpp)
    target_link_libraries(vpp-time-sleep-hardening PRIVATE vpp-native-aot vpp-runtime-native)
    if (MSVC)
        target_compile_options(vpp-time-sleep-hardening PRIVATE /W4 /utf-8)
    else()
        target_compile_options(vpp-time-sleep-hardening PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    add_test(NAME vpp-time-sleep-hardening COMMAND vpp-time-sleep-hardening)
    add_executable(vpp-time-clock-hardening scripts/quality/time_clock_hardening.cpp)
    target_link_libraries(vpp-time-clock-hardening PRIVATE vpp-native-aot vpp-runtime-native)
    if (MSVC)
        target_compile_options(vpp-time-clock-hardening PRIVATE /W4 /utf-8)
    else()
        target_compile_options(vpp-time-clock-hardening PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    add_test(NAME vpp-time-clock-hardening COMMAND vpp-time-clock-hardening)
    add_executable(vpp-entropy-machine-hardening scripts/quality/entropy_machine_hardening.cpp)
    target_link_libraries(vpp-entropy-machine-hardening PRIVATE vpp-native-aot vpp-runtime-native)
    if (MSVC)
        target_compile_options(vpp-entropy-machine-hardening PRIVATE /W4 /utf-8)
    else()
        target_compile_options(vpp-entropy-machine-hardening PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    add_test(NAME vpp-entropy-machine-hardening COMMAND vpp-entropy-machine-hardening)
    add_executable(vpp-environment-machine-hardening scripts/quality/environment_machine_hardening.cpp)
    target_link_libraries(vpp-environment-machine-hardening PRIVATE vpp-native-aot vpp-runtime-native)
    if (MSVC)
        target_compile_options(vpp-environment-machine-hardening PRIVATE /W4 /utf-8)
    else()
        target_compile_options(vpp-environment-machine-hardening PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    add_test(NAME vpp-environment-machine-hardening COMMAND vpp-environment-machine-hardening)
    add_executable(vpp-time-local-hardening scripts/quality/time_local_hardening.cpp)
    target_link_libraries(vpp-time-local-hardening PRIVATE vpp-native-aot vpp-runtime-native)
    if (MSVC)
        target_compile_options(vpp-time-local-hardening PRIVATE /W4 /utf-8)
    else()
        target_compile_options(vpp-time-local-hardening PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    add_test(NAME vpp-time-local-hardening COMMAND vpp-time-local-hardening)
    add_executable(vpp-directory-machine-hardening scripts/quality/directory_machine_hardening.cpp)
    target_link_libraries(vpp-directory-machine-hardening PRIVATE vpp-native-aot vpp-runtime-native)
    if (MSVC)
        target_compile_options(vpp-directory-machine-hardening PRIVATE /W4 /utf-8)
    else()
        target_compile_options(vpp-directory-machine-hardening PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    add_test(NAME vpp-directory-machine-hardening COMMAND vpp-directory-machine-hardening)
    add_executable(vpp-path-machine-hardening scripts/quality/path_machine_hardening.cpp)
    target_link_libraries(vpp-path-machine-hardening PRIVATE vpp-native-aot vpp-runtime-native)
    if (MSVC)
        target_compile_options(vpp-path-machine-hardening PRIVATE /W4 /utf-8)
    else()
        target_compile_options(vpp-path-machine-hardening PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    add_test(NAME vpp-path-machine-hardening COMMAND vpp-path-machine-hardening)
    add_executable(vpp-socket-machine-hardening scripts/quality/socket_machine_hardening.cpp)
    target_link_libraries(vpp-socket-machine-hardening PRIVATE vpp-native-aot vpp-runtime-native)
    if (MSVC)
        target_compile_options(vpp-socket-machine-hardening PRIVATE /W4 /utf-8)
    else()
        target_compile_options(vpp-socket-machine-hardening PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    add_test(NAME vpp-socket-machine-hardening COMMAND vpp-socket-machine-hardening)
    add_test(NAME vpp-socket-ip-parser-vpp-fixture
        COMMAND ${CMAKE_COMMAND}
            "-DVPP_EXEC=$<TARGET_FILE:vpp-cli>"
            "-DVPP_SOURCE_ROOT=${CMAKE_SOURCE_DIR}"
            -P ${CMAKE_SOURCE_DIR}/scripts/quality/check_socket_ip.cmake)
    add_test(NAME vpp-socket-hosts-vpp-fixture
        COMMAND ${CMAKE_COMMAND}
            "-DVPP_EXEC=$<TARGET_FILE:vpp-cli>"
            "-DVPP_SOURCE_ROOT=${CMAKE_SOURCE_DIR}"
            "-DVPP_BINARY_ROOT=${CMAKE_BINARY_DIR}"
            -P ${CMAKE_SOURCE_DIR}/scripts/quality/check_socket_hosts.cmake)
    add_test(NAME vpp-socket-dns-vi-vpp-fixture
        COMMAND ${CMAKE_COMMAND}
            "-DVPP_EXEC=$<TARGET_FILE:vpp-cli>"
            "-DVPP_SOURCE_ROOT=${CMAKE_SOURCE_DIR}"
            "-DVPP_BINARY_ROOT=${CMAKE_BINARY_DIR}"
            -P ${CMAKE_SOURCE_DIR}/scripts/quality/check_socket_dns_vi.cmake)
    add_test(NAME vpp-dns-wire-codec-vpp-fixture
        COMMAND ${CMAKE_COMMAND}
            "-DVPP_EXEC=$<TARGET_FILE:vpp-cli>"
            "-DVPP_SOURCE_ROOT=${CMAKE_SOURCE_DIR}"
            -P ${CMAKE_SOURCE_DIR}/scripts/quality/check_dns_wire.cmake)
    add_test(NAME vpp-dns-transport-vpp-fixture
        COMMAND ${CMAKE_COMMAND}
            "-DVPP_EXEC=$<TARGET_FILE:vpp-cli>"
            "-DVPP_SOURCE_ROOT=${CMAKE_SOURCE_DIR}"
            -P ${CMAKE_SOURCE_DIR}/scripts/quality/check_dns_transport.cmake)
    add_test(NAME vpp-dns-cache-vpp-fixture
        COMMAND ${CMAKE_COMMAND}
            "-DVPP_EXEC=$<TARGET_FILE:vpp-cli>"
            "-DVPP_SOURCE_ROOT=${CMAKE_SOURCE_DIR}"
            -P ${CMAKE_SOURCE_DIR}/scripts/quality/check_dns_cache.cmake)
    add_test(NAME vpp-dns-config-vpp-fixture
        COMMAND ${CMAKE_COMMAND}
            "-DVPP_EXEC=$<TARGET_FILE:vpp-cli>"
            "-DVPP_SOURCE_ROOT=${CMAKE_SOURCE_DIR}"
            -P ${CMAKE_SOURCE_DIR}/scripts/quality/check_dns_config.cmake)
    add_test(NAME vpp-dns-search-vpp-fixture
        COMMAND ${CMAKE_COMMAND}
            "-DVPP_EXEC=$<TARGET_FILE:vpp-cli>"
            "-DVPP_SOURCE_ROOT=${CMAKE_SOURCE_DIR}"
            -P ${CMAKE_SOURCE_DIR}/scripts/quality/check_dns_search.cmake)
    add_test(NAME vpp-dns-hosts-vpp-fixture
        COMMAND ${CMAKE_COMMAND}
            "-DVPP_EXEC=$<TARGET_FILE:vpp-cli>"
            "-DVPP_SOURCE_ROOT=${CMAKE_SOURCE_DIR}"
            -P ${CMAKE_SOURCE_DIR}/scripts/quality/check_dns_hosts.cmake)
    add_test(NAME vpp-dns-hosts-integration
        COMMAND ${CMAKE_COMMAND}
            "-DVPP_EXEC=$<TARGET_FILE:vpp-cli>"
            "-DVPP_SOURCE_ROOT=${CMAKE_SOURCE_DIR}"
            "-DVPP_BINARY_ROOT=${CMAKE_BINARY_DIR}"
            -P ${CMAKE_SOURCE_DIR}/scripts/quality/check_dns_hosts_integration.cmake)
    if (VPP_TEST_DNS_LOOPBACK)
        find_package(Python3 REQUIRED COMPONENTS Interpreter)
        add_test(NAME vpp-dns-tcp-loopback
            COMMAND "${Python3_EXECUTABLE}"
                ${CMAKE_SOURCE_DIR}/scripts/quality/check_dns_tcp_fallback.py
                $<TARGET_FILE:vpp-cli>)
        add_test(NAME vpp-dns-multiserver-loopback
            COMMAND "${Python3_EXECUTABLE}"
                ${CMAKE_SOURCE_DIR}/scripts/quality/check_dns_multiserver.py
                $<TARGET_FILE:vpp-cli>)
        add_test(NAME vpp-dns-structured-transport-loopback
            COMMAND "${Python3_EXECUTABLE}"
                ${CMAKE_SOURCE_DIR}/scripts/quality/check_dns_structured_transport.py
                $<TARGET_FILE:vpp-cli>)
    endif()
    add_test(NAME vpp-directory-vpp-fixture
        COMMAND $<TARGET_FILE:vpp-cli> ${CMAKE_SOURCE_DIR}/src/tests/kiem_tra_ffi_thu_muc.vi)
    if (UNIX)
        add_test(NAME vpp-time-local-vpp-fixture
            COMMAND ${CMAKE_COMMAND} -E env TZ=EST5EDT,M3.2.0/2,M11.1.0/2
                $<TARGET_FILE:vpp-cli> ${CMAKE_SOURCE_DIR}/src/tests/kiem_tra_ffi_gio_dia_phuong.vi)
    endif()
    add_test(NAME vpp-environment-vpp-fixture
        COMMAND ${CMAKE_COMMAND} -E env VPP_FFI_ENV_TEST=vpp-env-ffi-ok
            VPP_FFI_ENV_EMPTY_TEST= $<TARGET_FILE:vpp-cli>
            ${CMAKE_SOURCE_DIR}/src/tests/kiem_tra_ffi_moi_truong.vi)
    add_test(NAME vpp-entropy-vpp-fixture
        COMMAND $<TARGET_FILE:vpp-cli> ${CMAKE_SOURCE_DIR}/src/tests/kiem_tra_ffi_entropy.vi)
    add_test(NAME vpp-vi-asm-boundary-audit
        COMMAND sh ${CMAKE_SOURCE_DIR}/scripts/quality/audit_vi_asm_boundary.sh)

    add_executable(vpp-target-registry-hardening
        ${CMAKE_SOURCE_DIR}/scripts/quality/target_registry_hardening.cpp)
    target_link_libraries(vpp-target-registry-hardening PRIVATE vpp-runtime)
    target_include_directories(vpp-target-registry-hardening PRIVATE ${VPP_INCLUDE_DIR})
    add_test(NAME vpp-target-registry-hardening COMMAND vpp-target-registry-hardening)
    add_test(NAME vpp-target-vpp-registry
        COMMAND $<TARGET_FILE:vpp-cli>
            ${CMAKE_SOURCE_DIR}/src/tests/kiem_tra_kien_truc_vi.vi)
    add_test(NAME vpp-target-vpp-cpp-parity
        COMMAND $<TARGET_FILE:vpp-native-machine-cpp-oracle>
            --target-parity ${CMAKE_SOURCE_DIR}/src/tests/kiem_tra_kien_truc_vi.vi)
    add_test(NAME vpp-target-cli-hardening
        COMMAND ${CMAKE_COMMAND}
            "-DVPP_EXEC=$<TARGET_FILE:vpp-cli>"
            -P ${CMAKE_SOURCE_DIR}/scripts/quality/check_target_cli.cmake)
    # Target parsing compiles the V++ target registry package. This fixture
    # must find the same stdlib when the build tree is outside the checkout.
    set_tests_properties(vpp-target-cli-hardening PROPERTIES
        ENVIRONMENT "VPP_HOME=${CMAKE_SOURCE_DIR}")

    add_executable(vpp-raw-machine-codegen-hardening
        ${CMAKE_SOURCE_DIR}/scripts/quality/raw_machine_codegen_hardening.cpp)
    target_link_libraries(vpp-raw-machine-codegen-hardening PRIVATE vpp-runtime)
    target_include_directories(vpp-raw-machine-codegen-hardening PRIVATE ${VPP_INCLUDE_DIR})
    add_test(NAME vpp-raw-machine-codegen-hardening COMMAND vpp-raw-machine-codegen-hardening)
    add_executable(vpp-raw-object-hardening
        ${CMAKE_SOURCE_DIR}/scripts/quality/raw_object_hardening.cpp)
    target_link_libraries(vpp-raw-object-hardening PRIVATE vpp-runtime)
    target_include_directories(vpp-raw-object-hardening PRIVATE ${VPP_INCLUDE_DIR})
    add_test(NAME vpp-raw-object-hardening COMMAND vpp-raw-object-hardening)
    # Compare every emitted byte against the independent C++ object writer.
    add_executable(vpp-native-object-parity
        ${CMAKE_SOURCE_DIR}/scripts/quality/nativeobjectparity.cpp
        ${CMAKE_SOURCE_DIR}/src/cli/native_vpp_codegen.cpp)
    target_link_libraries(vpp-native-object-parity PRIVATE vpp-runtime vpp-native-aot)
    target_include_directories(vpp-native-object-parity PRIVATE ${VPP_INCLUDE_DIR})
    target_compile_features(vpp-native-object-parity PRIVATE cxx_std_17)
    add_test(NAME vpp-native-object-parity
        COMMAND $<TARGET_FILE:vpp-native-object-parity> ${CMAKE_SOURCE_DIR})
    set(VPP_NATIVE_SMOKE_TARGET "")
    if (APPLE AND CMAKE_SYSTEM_PROCESSOR MATCHES "^(arm64|aarch64)$")
        set(VPP_NATIVE_SMOKE_TARGET "aarch64-apple-darwin")
    elseif (APPLE AND CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|amd64)$")
        set(VPP_NATIVE_SMOKE_TARGET "x86_64-apple-darwin")
    elseif (CMAKE_SYSTEM_NAME STREQUAL "Linux" AND
            CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|amd64)$")
        set(VPP_NATIVE_SMOKE_TARGET "x86_64-linux-gnu")
    elseif (CMAKE_SYSTEM_NAME STREQUAL "Linux" AND
            CMAKE_SYSTEM_PROCESSOR MATCHES "^(arm64|aarch64)$")
        set(VPP_NATIVE_SMOKE_TARGET "aarch64-linux-gnu")
    endif()
    if (VPP_NATIVE_SMOKE_TARGET)
        add_test(NAME vpp-raw-object-host-link
            COMMAND ${CMAKE_COMMAND}
                "-DVPP_EXEC=$<TARGET_FILE:vpp-cli>"
                "-DVPP_SOURCE=${CMAKE_SOURCE_DIR}/src/tests/kiem_tra_native_leaf.vi"
                "-DVPP_CXX=${CMAKE_CXX_COMPILER}"
                "-DVPP_SMOKE_SOURCE=${CMAKE_SOURCE_DIR}/scripts/quality/native_object_link_smoke.cpp"
                "-DVPP_TARGET=${VPP_NATIVE_SMOKE_TARGET}"
                "-DVPP_OUTPUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/raw-native-link-fixtures"
                -P ${CMAKE_SOURCE_DIR}/scripts/quality/check_raw_object_link.cmake)
    endif()
    add_test(NAME vpp-raw-machine-cli-hardening
        COMMAND ${CMAKE_COMMAND}
            "-DVPP_EXEC=$<TARGET_FILE:vpp-cli>"
            "-DVPP_SOURCE=${CMAKE_SOURCE_DIR}/src/tests/kiem_tra_native_leaf.vi"
            "-DVPP_OUTPUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/raw-machine-fixtures"
            -P ${CMAKE_SOURCE_DIR}/scripts/quality/check_raw_machine_cli.cmake)
    add_test(NAME vpp-raw-machine-vpp-parity
        COMMAND $<TARGET_FILE:vpp-cli>
            ${CMAKE_SOURCE_DIR}/src/tests/kiem_tra_native_leaf_vi.vi)
    # Reference encoder is kept in C++ for independently checking the V++
    # production machine emitter during the self-host transition.
    add_executable(vpp-native-machine-cpp-oracle
        ${CMAKE_SOURCE_DIR}/scripts/quality/native_machine_cpp_oracle.cpp
        ${CMAKE_SOURCE_DIR}/src/cli/native_vpp_codegen.cpp)
    target_link_libraries(vpp-native-machine-cpp-oracle PRIVATE vpp-runtime vpp-native-aot)
    target_include_directories(vpp-native-machine-cpp-oracle PRIVATE ${VPP_INCLUDE_DIR})
    target_compile_features(vpp-native-machine-cpp-oracle PRIVATE cxx_std_17)
    add_test(NAME vpp-native-machine-lowering-parity
        COMMAND $<TARGET_FILE:vpp-native-machine-cpp-oracle>
            --lowering-parity
            ${CMAKE_SOURCE_DIR}/src/tests/kiem_tra_native_control_flow.vi
            ${CMAKE_SOURCE_DIR}/src/tests/kiem_tra_native_calls.vi
            ${CMAKE_SOURCE_DIR}/src/tests/kiem_tra_native_i32_boundaries.vi)
    add_test(NAME vpp-native-leaf-vpp-parity
        COMMAND $<TARGET_FILE:vpp-native-machine-cpp-oracle>
            --leaf-parity
            ${CMAKE_SOURCE_DIR}/src/tests/kiem_tra_native_leaf.vi)
    add_test(NAME vpp-native-machine-vpp-parity
        COMMAND ${CMAKE_COMMAND}
            "-DVPP_EXEC=$<TARGET_FILE:vpp-cli>"
            "-DVPP_CPP_ORACLE=$<TARGET_FILE:vpp-native-machine-cpp-oracle>"
            "-DVPP_VI_SOURCE=${CMAKE_SOURCE_DIR}/src/tests/kiem_tra_native_machine_vi.vi"
            "-DVPP_MACHINE_SOURCE=${CMAKE_SOURCE_DIR}/src/tests/kiem_tra_native_control_flow.vi"
            "-DVPP_CALL_SOURCE=${CMAKE_SOURCE_DIR}/src/tests/kiem_tra_native_calls.vi"
            "-DVPP_OUTPUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/machine-aarch64-vpp-parity"
            -P ${CMAKE_SOURCE_DIR}/scripts/quality/check_native_machine_vpp_parity.cmake)
    find_program(VPP_NATIVE_SMOKE_CC NAMES clang cc gcc)
    if (VPP_NATIVE_SMOKE_TARGET AND VPP_NATIVE_SMOKE_CC)
        # Runtime regressions share the native Stage 1 fixture with the
        # parser/codegen tests, avoiding repeated C++ Stage 0 compilation.
        set(VPP_NATIVE_HARNESS_STAGE1_ARG "")
        if ((APPLE AND CMAKE_SYSTEM_PROCESSOR MATCHES "^(arm64|aarch64)$") OR
            (CMAKE_SYSTEM_NAME STREQUAL "Linux" AND
             CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|amd64)$"))
            set(VPP_NATIVE_HARNESS_STAGE1 "${CMAKE_CURRENT_BINARY_DIR}/nativedriver/vpp-native")
            set(VPP_NATIVE_HARNESS_STAGE1_ARG "-DVPP_STAGE1=${VPP_NATIVE_HARNESS_STAGE1}")
        endif()
        add_test(NAME vpp-native-control-flow-host-link
            COMMAND ${CMAKE_COMMAND}
                "-DVPP_EXEC=$<TARGET_FILE:vpp-cli>"
                "-DVPP_SOURCE=${CMAKE_SOURCE_DIR}/src/tests/kiem_tra_native_control_flow.vi"
                "-DVPP_CC=${VPP_NATIVE_SMOKE_CC}"
                "-DVPP_SMOKE_SOURCE=${CMAKE_SOURCE_DIR}/scripts/quality/native_control_flow_smoke.c"
                "-DVPP_TARGET=${VPP_NATIVE_SMOKE_TARGET}"
                "-DVPP_OUTPUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/machine-native-link-fixtures"
                -P ${CMAKE_SOURCE_DIR}/scripts/quality/check_native_control_flow_link.cmake)
        add_test(NAME vpp-native-calls-host-link
            COMMAND ${CMAKE_COMMAND}
                "-DVPP_EXEC=$<TARGET_FILE:vpp-cli>"
                "-DVPP_SOURCE=${CMAKE_SOURCE_DIR}/src/tests/kiem_tra_native_calls.vi"
                "-DVPP_CC=${VPP_NATIVE_SMOKE_CC}"
                "-DVPP_SMOKE_SOURCE=${CMAKE_SOURCE_DIR}/scripts/quality/native_calls_smoke.c"
                "-DVPP_TARGET=${VPP_NATIVE_SMOKE_TARGET}"
                "-DVPP_OUTPUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/machine-native-calls-fixtures"
                -P ${CMAKE_SOURCE_DIR}/scripts/quality/check_native_calls_link.cmake)
        add_test(NAME vpp-native-selfhost-host-link
            COMMAND ${CMAKE_COMMAND}
                "-DVPP_EXEC=$<TARGET_FILE:vpp-cli>"
                "-DVPP_SOURCE=${CMAKE_SOURCE_DIR}/src/tests/nativeaotsmoke.vi"
                "-DVPP_CC=${VPP_NATIVE_SMOKE_CC}"
                "-DVPP_SMOKE_SOURCE=${CMAKE_SOURCE_DIR}/scripts/quality/nativeaotsmoke.c"
                "-DVPP_TARGET=${VPP_NATIVE_SMOKE_TARGET}"
                "-DVPP_HOME=${CMAKE_SOURCE_DIR}"
                "-DVPP_OUTPUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/nativeselfhost"
                -P ${CMAKE_SOURCE_DIR}/scripts/quality/checknativeselfhost.cmake)
        add_test(NAME vpp-native-selfhost-executable
            COMMAND ${CMAKE_COMMAND}
                "-DVPP_EXEC=$<TARGET_FILE:vpp-cli>"
                "-DVPP_SOURCE=${CMAKE_SOURCE_DIR}/src/tests/nativeaotprogram.vi"
                "-DVPP_TARGET=${VPP_NATIVE_SMOKE_TARGET}"
                "-DVPP_HOME=${CMAKE_SOURCE_DIR}"
                "-DVPP_OUTPUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/nativeprogram"
                ${VPP_NATIVE_HARNESS_STAGE1_ARG}
                -P ${CMAKE_SOURCE_DIR}/scripts/quality/checknativeprogram.cmake)
        add_test(NAME vpp-native-i32-runtime
            COMMAND ${CMAKE_COMMAND}
                "-DVPP_EXEC=$<TARGET_FILE:vpp-cli>"
                "-DVPP_SOURCE=${CMAKE_SOURCE_DIR}/src/tests/nativeaotprint.vi"
                "-DVPP_TARGET=${VPP_NATIVE_SMOKE_TARGET}"
                "-DVPP_HOME=${CMAKE_SOURCE_DIR}"
                "-DVPP_OUTPUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/nativeprint"
                ${VPP_NATIVE_HARNESS_STAGE1_ARG}
                -P ${CMAKE_SOURCE_DIR}/scripts/quality/checknativeprint.cmake)
        add_test(NAME vpp-native-i32-input
            COMMAND ${CMAKE_COMMAND}
                "-DVPP_EXEC=$<TARGET_FILE:vpp-cli>"
                "-DVPP_SOURCE=${CMAKE_SOURCE_DIR}/src/tests/nativeaotread.vi"
                "-DVPP_TARGET=${VPP_NATIVE_SMOKE_TARGET}"
                "-DVPP_HOME=${CMAKE_SOURCE_DIR}"
                "-DVPP_OUTPUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/nativeread"
                ${VPP_NATIVE_HARNESS_STAGE1_ARG}
                -P ${CMAKE_SOURCE_DIR}/scripts/quality/checknativeread.cmake)
        add_test(NAME vpp-native-byte-file-io
            COMMAND ${CMAKE_COMMAND}
                "-DVPP_EXEC=$<TARGET_FILE:vpp-cli>"
                "-DVPP_SOURCE=${CMAKE_SOURCE_DIR}/src/tests/nativeaotbytefile.vi"
                "-DVPP_TARGET=${VPP_NATIVE_SMOKE_TARGET}"
                "-DVPP_HOME=${CMAKE_SOURCE_DIR}"
                "-DVPP_OUTPUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/nativebytefile"
                ${VPP_NATIVE_HARNESS_STAGE1_ARG}
                -P ${CMAKE_SOURCE_DIR}/scripts/quality/checknativebytefile.cmake)
        add_test(NAME vpp-native-block-file-io
            COMMAND ${CMAKE_COMMAND}
                "-DVPP_EXEC=$<TARGET_FILE:vpp-cli>"
                "-DVPP_SOURCE=${CMAKE_SOURCE_DIR}/src/tests/nativeaotbulk.vi"
                "-DVPP_TARGET=${VPP_NATIVE_SMOKE_TARGET}"
                "-DVPP_HOME=${CMAKE_SOURCE_DIR}"
                "-DVPP_OUTPUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/nativebulk"
                ${VPP_NATIVE_HARNESS_STAGE1_ARG}
                -P ${CMAKE_SOURCE_DIR}/scripts/quality/checknativebulk.cmake)
        add_test(NAME vpp-native-byte-ranges
            COMMAND ${CMAKE_COMMAND}
                "-DVPP_EXEC=$<TARGET_FILE:vpp-cli>"
                "-DVPP_SOURCE=${CMAKE_SOURCE_DIR}/src/tests/nativeaotbytesops.vi"
                "-DVPP_TARGET=${VPP_NATIVE_SMOKE_TARGET}"
                "-DVPP_HOME=${CMAKE_SOURCE_DIR}"
                "-DVPP_OUTPUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/nativebyteranges"
                ${VPP_NATIVE_HARNESS_STAGE1_ARG}
                -P ${CMAKE_SOURCE_DIR}/scripts/quality/checknativebytesops.cmake)
        # The lexer is already written in V++; exercise it with the prepared
        # native compiler instead of starting another expensive Stage 0 build.
        add_test(NAME vpp-native-lexer-kernel
            COMMAND ${CMAKE_COMMAND}
                "-DVPP_EXEC=$<TARGET_FILE:vpp-cli>"
                "-DVPP_KERNEL=${CMAKE_SOURCE_DIR}/gói/biên dịch/native/lexer.vi"
                "-DVPP_HARNESS=${CMAKE_SOURCE_DIR}/src/tests/nativelexerharness.vi"
                "-DVPP_TARGET=${VPP_NATIVE_SMOKE_TARGET}"
                "-DVPP_HOME=${CMAKE_SOURCE_DIR}"
                "-DVPP_OUTPUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/nativelexer"
                ${VPP_NATIVE_HARNESS_STAGE1_ARG}
                -P ${CMAKE_SOURCE_DIR}/scripts/quality/checknativelexer.cmake)
        # Build the ASM-backed V++ Stage 1 compiler once for the native
        # parser/IR, Mach-O and cross-host ELF regressions. Running separate
        # C++ Stage 0 bootstraps in parallel routinely times out in CI.
        if (VPP_NATIVE_HARNESS_STAGE1_ARG)
            add_test(NAME vpp-native-stage1-bootstrap
                COMMAND ${CMAKE_COMMAND}
                    "-DVPP_EXEC=$<TARGET_FILE:vpp-cli>"
                    "-DVPP_ROOT=${CMAKE_SOURCE_DIR}"
                    "-DVPP_TARGET=${VPP_NATIVE_SMOKE_TARGET}"
                    "-DVPP_OUTPUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/nativedriver"
                    "-DVPP_PREPARE_ONLY=1"
                    -P ${CMAKE_SOURCE_DIR}/scripts/quality/checknativedriver.cmake)
            set_tests_properties(vpp-native-stage1-bootstrap PROPERTIES
                FIXTURES_SETUP native-stage1 TIMEOUT 540 RUN_SERIAL TRUE)
            set_tests_properties(vpp-native-lexer-kernel PROPERTIES
                FIXTURES_REQUIRED native-stage1)
            set_tests_properties(
                vpp-native-i32-runtime
                vpp-native-selfhost-executable
                vpp-native-i32-input
                vpp-native-byte-file-io
                vpp-native-block-file-io
                vpp-native-byte-ranges
                PROPERTIES FIXTURES_REQUIRED native-stage1)
        endif()
        add_test(NAME vpp-native-parser-ir
            COMMAND ${CMAKE_COMMAND}
                "-DVPP_EXEC=$<TARGET_FILE:vpp-cli>"
                "-DVPP_ROOT=${CMAKE_SOURCE_DIR}"
                "-DVPP_TARGET=${VPP_NATIVE_SMOKE_TARGET}"
                "-DVPP_OUTPUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/nativeparser"
                ${VPP_NATIVE_HARNESS_STAGE1_ARG}
                -P ${CMAKE_SOURCE_DIR}/scripts/quality/checknativeparser.cmake)
        if (VPP_NATIVE_HARNESS_STAGE1_ARG)
            set_tests_properties(vpp-native-parser-ir PROPERTIES FIXTURES_REQUIRED native-stage1)
        endif()
        if ((APPLE AND CMAKE_SYSTEM_PROCESSOR MATCHES "^(arm64|aarch64)$") OR
            (CMAKE_SYSTEM_NAME STREQUAL "Linux" AND CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|amd64)$"))
            add_test(NAME vpp-native-elf-x86-writer
                COMMAND ${CMAKE_COMMAND}
                    "-DVPP_EXEC=$<TARGET_FILE:vpp-cli>"
                    "-DVPP_ROOT=${CMAKE_SOURCE_DIR}"
                    "-DVPP_TARGET=${VPP_NATIVE_SMOKE_TARGET}"
                    "-DVPP_OUTPUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/native-elf-x86"
                    ${VPP_NATIVE_HARNESS_STAGE1_ARG}
                    -P ${CMAKE_SOURCE_DIR}/scripts/quality/check_native_elf_x86.cmake)
            set_tests_properties(vpp-native-elf-x86-writer PROPERTIES
                FIXTURES_REQUIRED native-stage1)
            add_test(NAME vpp-native-compiler-driver
                COMMAND ${CMAKE_COMMAND}
                    "-DVPP_EXEC=$<TARGET_FILE:vpp-cli>"
                    "-DVPP_ROOT=${CMAKE_SOURCE_DIR}"
                    "-DVPP_TARGET=${VPP_NATIVE_SMOKE_TARGET}"
                    "-DVPP_OUTPUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/nativedriver"
                    "-DVPP_USE_PREPARED_COMPILER=1"
                    -P ${CMAKE_SOURCE_DIR}/scripts/quality/checknativedriver.cmake)
            set_tests_properties(vpp-native-compiler-driver PROPERTIES
                FIXTURES_REQUIRED native-stage1)
        endif()
        if (APPLE AND CMAKE_SYSTEM_PROCESSOR MATCHES "^(arm64|aarch64)$")
            add_test(NAME vpp-native-ir-object
                COMMAND ${CMAKE_COMMAND}
                    "-DVPP_EXEC=$<TARGET_FILE:vpp-cli>"
                    "-DVPP_ROOT=${CMAKE_SOURCE_DIR}"
                    "-DVPP_TARGET=${VPP_NATIVE_SMOKE_TARGET}"
                    "-DVPP_OUTPUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/nativecodegen"
                    ${VPP_NATIVE_HARNESS_STAGE1_ARG}
                    -P ${CMAKE_SOURCE_DIR}/scripts/quality/checknativecodegen.cmake)
            set_tests_properties(vpp-native-ir-object PROPERTIES
                FIXTURES_REQUIRED native-stage1)
            add_test(NAME vpp-native-vpp-runtime
                COMMAND ${CMAKE_COMMAND}
                    "-DVPP_EXEC=$<TARGET_FILE:vpp-cli>"
                    "-DVPP_ROOT=${CMAKE_SOURCE_DIR}"
                    "-DVPP_TARGET=${VPP_NATIVE_SMOKE_TARGET}"
                    "-DVPP_OUTPUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/native-runtime-check"
                    ${VPP_NATIVE_HARNESS_STAGE1_ARG}
                    -P ${CMAKE_SOURCE_DIR}/scripts/quality/check_native_runtime.cmake)
            set_tests_properties(vpp-native-vpp-runtime PROPERTIES
                FIXTURES_REQUIRED native-stage1)
        endif()
    endif()
    # MSVC needs the Visual Studio build environment to link the generated
    # COFF .obj files. Generate and link these as a normal CMake target, so
    # MSBuild provides the compiler/linker environment and Windows ABI checks
    # actually execute the resulting .exe in CTest.
    if (WIN32 AND MSVC AND CMAKE_SIZEOF_VOID_P EQUAL 8)
        set(VPP_WIN64_NATIVE_OBJ_DIR "${CMAKE_CURRENT_BINARY_DIR}/native-win64-calls")
        set(VPP_WIN64_NATIVE_CALL_OBJECTS)
        foreach(func IN ITEMS co_so them ten_ham_dai nhan_sau ket_hop tinh_vong bon goi_bon tong_muoi goi_muoi)
            set(obj "${VPP_WIN64_NATIVE_OBJ_DIR}/${func}.obj")
            add_custom_command(OUTPUT "${obj}"
                COMMAND ${CMAKE_COMMAND} -E make_directory "${VPP_WIN64_NATIVE_OBJ_DIR}"
                COMMAND $<TARGET_FILE:vpp-cli> build --target x86_64-windows-msvc
                    --emit-obj "${func}"
                    "${CMAKE_SOURCE_DIR}/src/tests/kiem_tra_native_calls.vi" "${obj}"
                DEPENDS vpp-cli "${CMAKE_SOURCE_DIR}/src/tests/kiem_tra_native_calls.vi"
                VERBATIM)
            set_source_files_properties("${obj}" PROPERTIES GENERATED TRUE EXTERNAL_OBJECT TRUE)
            list(APPEND VPP_WIN64_NATIVE_CALL_OBJECTS "${obj}")
        endforeach()
        # The project enables CXX (without a C compiler). Preserve the C ABI
        # declarations when compiling this small harness as C++.
        set_source_files_properties(
            "${CMAKE_SOURCE_DIR}/scripts/quality/native_calls_smoke.c"
            PROPERTIES LANGUAGE CXX)
        add_executable(vpp-native-win64-calls-smoke
            "${CMAKE_SOURCE_DIR}/scripts/quality/native_calls_smoke.c"
            ${VPP_WIN64_NATIVE_CALL_OBJECTS})
        add_test(NAME vpp-native-calls-host-link
            COMMAND $<TARGET_FILE:vpp-native-win64-calls-smoke>)
    endif()
    find_program(VPP_NATIVE_ASSEMBLER_CLANG NAMES clang)
    add_test(NAME vpp-library-visibility
        COMMAND ${CMAKE_COMMAND} "-DVPP_ROOT=${CMAKE_SOURCE_DIR}"
            -P ${CMAKE_SOURCE_DIR}/scripts/quality/check_library_visibility.cmake)
    add_test(NAME vpp-opcode-abi
        COMMAND ${CMAKE_COMMAND} "-DVPP_ROOT=${CMAKE_SOURCE_DIR}"
            -P ${CMAKE_SOURCE_DIR}/scripts/quality/check_vpp_opcodes.cmake)
    # Full-language Stage 0 emits a native compiler for Unix x64/ARM64 and
    # Windows x64. Every --compiler=vpp regression needs that executable.
    if ((UNIX AND CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|amd64|aarch64|arm64)$") OR
        (WIN32 AND MSVC AND CMAKE_SIZEOF_VOID_P EQUAL 8))
        set(VPP_NATIVE_CLI_COMPILER "${CMAKE_CURRENT_BINARY_DIR}/native-compiler/vpp-native${CMAKE_EXECUTABLE_SUFFIX}")
        add_test(NAME vpp-native-cli-compiler-bootstrap
            COMMAND ${CMAKE_COMMAND}
                "-DVPP_CLI=$<TARGET_FILE:vpp-cli>"
                "-DVPP_ROOT=${CMAKE_SOURCE_DIR}"
                "-DVPP_OUTPUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/native-compiler"
                "-DVPP_EXECUTABLE_SUFFIX=${CMAKE_EXECUTABLE_SUFFIX}"
                -P ${CMAKE_SOURCE_DIR}/scripts/quality/prepare_native_compiler.cmake)
        set_tests_properties(vpp-native-cli-compiler-bootstrap PROPERTIES
            FIXTURES_SETUP native-cli-compiler TIMEOUT 420)
        if (UNIX)
            # System packages require the full-language compiler. The small
            # native-stage1 leaf compiler cannot parse imports or Machine ABI.
            add_test(NAME vpp-native-machine-system-fixtures
                COMMAND ${CMAKE_COMMAND}
                    "-DVPP_NATIVE_CLI=${VPP_NATIVE_CLI_COMPILER}"
                    "-DVPP_ROOT=${CMAKE_SOURCE_DIR}"
                    "-DVPP_OUTPUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/native-machine-system"
                    -P ${CMAKE_SOURCE_DIR}/scripts/quality/check_native_machine_fixtures.cmake)
            set_tests_properties(vpp-native-machine-system-fixtures PROPERTIES
                FIXTURES_REQUIRED native-cli-compiler TIMEOUT 1200)
        endif()
    endif()
    foreach(vpp_case IN ITEMS compiler oop ffi)
        if (vpp_case STREQUAL "compiler")
            set(vpp_cli_test vpp-compiler-vpp-cli)
        elseif (vpp_case STREQUAL "ffi")
            set(vpp_cli_test vpp-compiler-ffi-artifact)
        else()
            set(vpp_cli_test vpp-oop-pipeline)
        endif()
        add_test(NAME ${vpp_cli_test}
            COMMAND ${CMAKE_COMMAND}
                "-DVPP_EXEC=$<TARGET_FILE:vpp-cli>"
                "-DVPP_ROOT=${CMAKE_SOURCE_DIR}"
                "-DVPP_CASE=${vpp_case}"
                "-DVPP_OUTPUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/vpp-cli-${vpp_case}"
                "-DVPP_NATIVE_COMPILER=${VPP_NATIVE_CLI_COMPILER}"
                -P ${CMAKE_SOURCE_DIR}/scripts/quality/check_vpp_cli.cmake)
        set_tests_properties(${vpp_cli_test} PROPERTIES TIMEOUT 1200)
        if (VPP_NATIVE_CLI_COMPILER)
            set_tests_properties(${vpp_cli_test} PROPERTIES FIXTURES_REQUIRED native-cli-compiler)
        endif()
    endforeach()
    if (VPP_NATIVE_ASSEMBLER_CLANG)
        add_test(NAME vpp-native-real-assembler-smoke
            COMMAND ${CMAKE_COMMAND}
                "-DVPP_EXEC=$<TARGET_FILE:vpp-cli>"
                "-DVPP_CLANG=${VPP_NATIVE_ASSEMBLER_CLANG}"
                "-DVPP_LEAF=${CMAKE_SOURCE_DIR}/src/tests/kiem_tra_native_leaf.vi"
                "-DVPP_GENERAL=${CMAKE_SOURCE_DIR}/src/tests/kiem_tra_native_control_flow.vi"
                "-DVPP_OUTPUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/real-native-assembler-fixtures"
                -P ${CMAKE_SOURCE_DIR}/scripts/quality/check_native_assembler.cmake)
        if (UNIX)
            add_test(NAME vpp-aot-platform-primitives-asm
                COMMAND ${CMAKE_COMMAND} -E env
                    "VPP_CLANG=${VPP_NATIVE_ASSEMBLER_CLANG}"
                    /bin/sh ${CMAKE_SOURCE_DIR}/scripts/quality/check_aot_primitives.sh)
        endif()
    endif()

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
install(TARGETS vdk vpp-cli vpp-core vpp-frontend vpp-bytecode vpp-compiler vpp-runtime vpp-runtime-native vpp-tooling vpp-native-aot
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
install(FILES
        # Retain the C source only for native AOT bootstrap/fallback targets.
        # macOS ARM64 self-hosting uses runtime.vi + the assembly primitives.
        ${CMAKE_SOURCE_DIR}/src/runtime/native/vpp_aot_runtime.c
        DESTINATION src/runtime/native)
install(DIRECTORY ${CMAKE_SOURCE_DIR}/scripts/ASM/
        DESTINATION scripts/ASM
        FILES_MATCHING
        PATTERN "*.S"
        PATTERN "*.inc"
        PATTERN "README.md")
install(DIRECTORY ${CMAKE_SOURCE_DIR}/scripts/quality/native_aot_link/
        DESTINATION scripts/quality/native_aot_link)
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
