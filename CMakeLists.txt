cmake_minimum_required(VERSION 3.15)
project(VPP LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

option(BUILD_TESTS "Build unit tests" ON)
option(BUILD_SHARED_LIBS "Build shared libraries instead of static" OFF)

find_package(Threads REQUIRED)

# Public include dir for headers
set(VPP_INCLUDE_DIR ${CMAKE_SOURCE_DIR}/include)

# Output directories for libs and binaries
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# Explicit ownership keeps target dependencies directional; no target is
# allowed to use the old catch-all `helpers` grouping.
set(VPP_CORE_SOURCES
    ${CMAKE_SOURCE_DIR}/src/core/text.cpp)
set(VPP_FRONTEND_SOURCES
    ${CMAKE_SOURCE_DIR}/src/frontend/keywords.cpp
    ${CMAKE_SOURCE_DIR}/src/frontend/lexer.cpp
    ${CMAKE_SOURCE_DIR}/src/frontend/ast.cpp
    ${CMAKE_SOURCE_DIR}/src/frontend/parser.cpp)
set(VPP_BYTECODE_SOURCES
    ${CMAKE_SOURCE_DIR}/src/bytecode/opcode.cpp)
set(VPP_COMPILER_SOURCES
    ${CMAKE_SOURCE_DIR}/src/compiler/support/expression.cpp
    ${CMAKE_SOURCE_DIR}/src/compiler/support/storeString.cpp
    ${CMAKE_SOURCE_DIR}/src/compiler/support/symbolTable.cpp
    ${CMAKE_SOURCE_DIR}/src/compiler/support/token_utils.cpp
    ${CMAKE_SOURCE_DIR}/src/compiler/semantic.cpp
    ${CMAKE_SOURCE_DIR}/src/compiler/ir.cpp
    ${CMAKE_SOURCE_DIR}/src/compiler/optimizer.cpp
    ${CMAKE_SOURCE_DIR}/src/compiler/compileBlock.cpp
    ${CMAKE_SOURCE_DIR}/src/compiler/compileCondition.cpp
    ${CMAKE_SOURCE_DIR}/src/compiler/compileFunction.cpp
    ${CMAKE_SOURCE_DIR}/src/compiler/compileLoop.cpp
    ${CMAKE_SOURCE_DIR}/src/compiler/compileRegistry.cpp
    ${CMAKE_SOURCE_DIR}/src/compiler/compileStatement.cpp
    ${CMAKE_SOURCE_DIR}/src/compiler/compileSwitch.cpp
    ${CMAKE_SOURCE_DIR}/src/compiler/compiler.cpp
    ${CMAKE_SOURCE_DIR}/src/compiler/compilerExpr.cpp)
set(VPP_RUNTIME_SOURCES
    ${CMAKE_SOURCE_DIR}/src/runtime/vm.cpp
    ${CMAKE_SOURCE_DIR}/src/runtime/native/vm_native_http_helpers.cpp
    ${CMAKE_SOURCE_DIR}/src/runtime/native/vm_native_helpers.cpp
    ${CMAKE_SOURCE_DIR}/src/runtime/native/vm_low_level_http_server.cpp)
set(VPP_TOOLING_SOURCES
    ${CMAKE_SOURCE_DIR}/src/tooling/tooling.cpp)
set(VPP_CLI_SOURCES ${CMAKE_SOURCE_DIR}/src/cli/main.cpp)

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
target_link_libraries(vpp-runtime PUBLIC vpp-bytecode vpp-core Threads::Threads)
if (WIN32)
    target_link_libraries(vpp-runtime PUBLIC ws2_32)
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

# C++ unit-test sources live in test/; src/tests/ is reserved for V++ .vi
# regression programs and their runtime fixtures.
if (BUILD_TESTS)
    enable_testing()
    if (EXISTS ${CMAKE_SOURCE_DIR}/test/CMakeLists.txt)
        add_subdirectory(test)
    endif()
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

# Install rules (optional)
install(TARGETS vpp-cli vpp-core vpp-frontend vpp-bytecode vpp-compiler vpp-runtime vpp-tooling
        RUNTIME DESTINATION bin
        ARCHIVE DESTINATION lib
        LIBRARY DESTINATION lib)
install(DIRECTORY ${CMAKE_SOURCE_DIR}/gói/
        DESTINATION gói
        PATTERN ".DS_Store" EXCLUDE)
install(DIRECTORY ${CMAKE_SOURCE_DIR}/templates/
        DESTINATION templates
        PATTERN ".DS_Store" EXCLUDE)
install(DIRECTORY ${CMAKE_SOURCE_DIR}/examples/
        DESTINATION examples
        PATTERN ".DS_Store" EXCLUDE)
install(DIRECTORY ${CMAKE_SOURCE_DIR}/include/
        DESTINATION include
        PATTERN ".DS_Store" EXCLUDE)

# Developer notes:
# - Keep source ownership explicit; do not add new files to a generic helpers target.
# - Move public headers to include/vpp/<module>/ as modules stabilize.
# - Keep dependencies pointing toward lower-level modules only.
