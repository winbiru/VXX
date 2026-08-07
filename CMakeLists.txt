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

# Helper: collect sources per module (prefer explicit lists for production)
file(GLOB_RECURSE FRONTEND_SOURCES CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/src/frontend/*.cpp ${CMAKE_SOURCE_DIR}/src/helpers/*.cpp)
file(GLOB_RECURSE COMPILER_SOURCES CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/src/compiler/*.cpp ${CMAKE_SOURCE_DIR}/src/compiler/*.cxx ${CMAKE_SOURCE_DIR}/src/compiler/*.cc)
file(GLOB_RECURSE VM_SOURCES CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/src/vm/*.cpp)
file(GLOB_RECURSE STDLIB_SOURCES CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/src/stdlib/*.cpp)
set(CLI_SOURCES ${CMAKE_SOURCE_DIR}/src/cli/main.cpp)

# VM-specific helpers should live with VM linkage, not frontend linkage.
set(VM_NATIVE_HELPERS
    ${CMAKE_SOURCE_DIR}/src/helpers/vm_native_http_helpers.cpp
    ${CMAKE_SOURCE_DIR}/src/helpers/vm_native_helpers.cpp
    ${CMAKE_SOURCE_DIR}/src/helpers/vm_low_level_http_server.cpp)
list(REMOVE_ITEM FRONTEND_SOURCES ${VM_NATIVE_HELPERS})
list(APPEND VM_SOURCES ${VM_NATIVE_HELPERS})

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
        target_compile_options(${name} PRIVATE /W4 /permissive-)
    else()
        target_compile_options(${name} PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endfunction()

# Create libraries per module only if sources exist
if (FRONTEND_SOURCES)
    vpp_add_module(vpp-frontend ${FRONTEND_SOURCES})
else()
    message(FATAL_ERROR "No frontend sources found in src/frontend")
endif()

if (COMPILER_SOURCES)
    vpp_add_module(vpp-compiler ${COMPILER_SOURCES})
    target_link_libraries(vpp-compiler PUBLIC vpp-frontend)
endif()

if (VM_SOURCES)
    vpp_add_module(vpp-vm ${VM_SOURCES})
    target_link_libraries(vpp-vm PUBLIC vpp-compiler Threads::Threads)
    if (WIN32)
        target_link_libraries(vpp-vm PUBLIC ws2_32)
    endif()
endif()

# stdlib is optional: only create if there are sources
if (STDLIB_SOURCES)
    vpp_add_module(vpp-stdlib ${STDLIB_SOURCES})
    target_link_libraries(vpp-stdlib PUBLIC vpp-vm)
else()
    message(STATUS "No stdlib sources found; skipping vpp-stdlib")
endif()

# CLI executable (thin wrapper that uses libs above)
add_executable(vpp-cli ${CLI_SOURCES})

# Link only to the top-level library - transitive dependencies will pull in the rest
# This avoids duplicate library warnings from the linker
if (TARGET vpp-stdlib)
    target_link_libraries(vpp-cli PRIVATE vpp-stdlib)
else()
    target_link_libraries(vpp-cli PRIVATE vpp-vm)
endif()
target_include_directories(vpp-cli PRIVATE ${VPP_INCLUDE_DIR})

if (MSVC)
    target_compile_definitions(vpp-cli PRIVATE
        _CRT_SECURE_NO_WARNINGS
        _CRT_NONSTDC_NO_WARNINGS
        popen=_popen
        pclose=_pclose
    )
    target_compile_options(vpp-cli PRIVATE /W4 /permissive-)
else()
    target_compile_options(vpp-cli PRIVATE -Wall -Wextra -Wpedantic)
endif()

# Backward-compat: keep legacy binary name available for existing scripts.
add_custom_command(TARGET vpp-cli POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:vpp-cli>
            ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/vietvm-cli)

# Tests (unit tests in src/tests) and integration runner if present
if (BUILD_TESTS)
    enable_testing()
    if (EXISTS ${CMAKE_SOURCE_DIR}/src/tests/CMakeLists.txt)
        add_subdirectory(src/tests)
    endif()
    if (UNIX AND EXISTS ${CMAKE_SOURCE_DIR}/run_tests.sh)
        add_test(NAME vpp-integration
                 COMMAND ${CMAKE_SOURCE_DIR}/run_tests.sh)
        set_tests_properties(vpp-integration PROPERTIES
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
    endif()
endif()

# Install rules (optional)
install(TARGETS vpp-cli vpp-frontend vpp-compiler vpp-vm
        RUNTIME DESTINATION bin
        ARCHIVE DESTINATION lib
        LIBRARY DESTINATION lib)
install(DIRECTORY ${CMAKE_SOURCE_DIR}/gói/
        DESTINATION gói)

# Developer notes:
# - Prefer explicit source lists; GLOB is fine for prototyping but can hide missing files.
# - Move public headers to include/vietvm/ and include them as <vietvm/...>.
# - Use target-based API as above.
