cmake_minimum_required(VERSION 3.15)
project(VietVM LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

option(BUILD_TESTS "Build unit tests" ON)
option(BUILD_SHARED_LIBS "Build shared libraries instead of static" OFF)

# Public include dir for headers
set(VIETVM_INCLUDE_DIR ${CMAKE_SOURCE_DIR}/include)

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

# Create library helper (accepts sources passed directly)
function(vietvm_add_module name)
    set(sources ${ARGN})
    if (NOT sources)
        message(FATAL_ERROR "No sources provided for module ${name}")
    endif()
    if (BUILD_SHARED_LIBS)
        add_library(${name} SHARED ${sources})
    else()
        add_library(${name} STATIC ${sources})
    endif()
    target_include_directories(${name} PUBLIC ${VIETVM_INCLUDE_DIR})
    target_compile_features(${name} PUBLIC cxx_std_17)
    if (MSVC)
        target_compile_options(${name} PRIVATE /W4 /permissive-)
    else()
        target_compile_options(${name} PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endfunction()

# Create libraries per module only if sources exist
if (FRONTEND_SOURCES)
    vietvm_add_module(vietvm-frontend ${FRONTEND_SOURCES})
else()
    message(FATAL_ERROR "No frontend sources found in src/frontend")
endif()

if (COMPILER_SOURCES)
    vietvm_add_module(vietvm-compiler ${COMPILER_SOURCES})
    target_link_libraries(vietvm-compiler PUBLIC vietvm-frontend)
endif()

if (VM_SOURCES)
    vietvm_add_module(vietvm-vm ${VM_SOURCES})
    target_link_libraries(vietvm-vm PUBLIC vietvm-compiler)
endif()

# stdlib is optional: only create if there are sources
if (STDLIB_SOURCES)
    vietvm_add_module(vietvm-stdlib ${STDLIB_SOURCES})
    target_link_libraries(vietvm-stdlib PUBLIC vietvm-vm)
else()
    message(STATUS "No stdlib sources found; skipping vietvm-stdlib")
endif()

# CLI executable (thin wrapper that uses libs above)
add_executable(vietvm-cli ${CLI_SOURCES})

# Link only to the top-level library - transitive dependencies will pull in the rest
# This avoids duplicate library warnings from the linker
if (TARGET vietvm-stdlib)
    target_link_libraries(vietvm-cli PRIVATE vietvm-stdlib)
else()
    target_link_libraries(vietvm-cli PRIVATE vietvm-vm)
endif()
target_include_directories(vietvm-cli PRIVATE ${VIETVM_INCLUDE_DIR})

if (MSVC)
    target_compile_options(vietvm-cli PRIVATE /W4 /permissive-)
else()
    target_compile_options(vietvm-cli PRIVATE -Wall -Wextra -Wpedantic)
endif()

# Tests (unit tests in src/tests) and integration runner if present
if (BUILD_TESTS)
    enable_testing()
    if (EXISTS ${CMAKE_SOURCE_DIR}/src/tests/CMakeLists.txt)
        add_subdirectory(src/tests)
    endif()
endif()

# Install rules (optional)
install(TARGETS vietvm-cli vietvm-frontend vietvm-compiler vietvm-vm
        RUNTIME DESTINATION bin
        ARCHIVE DESTINATION lib
        LIBRARY DESTINATION lib)

# Developer notes:
# - Prefer explicit source lists; GLOB is fine for prototyping but can hide missing files.
# - Move public headers to include/vietvm/ and include them as <vietvm/...>.
# - Use target-based API as above.