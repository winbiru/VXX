add_executable(vpp-benchmark-baseline
    benchmark_baseline.cpp telemetry.cpp)

# Reject accidental timing of sanitizer/coverage builds. The runner also checks
# the effective configuration recorded in this generated header.
set(VPP_BENCHMARK_FLAGS "${CMAKE_CXX_FLAGS} ${CMAKE_CXX_FLAGS_RELEASE}")
configure_file(benchmark_config.h.in benchmark_config.h @ONLY)
target_include_directories(vpp-benchmark-baseline PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
if (WIN32)
    target_link_libraries(vpp-benchmark-baseline PRIVATE psapi)
endif()

target_link_libraries(vpp-benchmark-baseline PRIVATE
    vpp-compiler
    vpp-runtime)
target_include_directories(vpp-benchmark-baseline PRIVATE ${VPP_INCLUDE_DIR})
target_compile_features(vpp-benchmark-baseline PRIVATE cxx_std_17)

if (MSVC)
    target_compile_options(vpp-benchmark-baseline PRIVATE /W4 /permissive- /utf-8)
else()
    target_compile_options(vpp-benchmark-baseline PRIVATE -Wall -Wextra -Wpedantic)
endif()
