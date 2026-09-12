add_executable(vpp-benchmark-baseline
    benchmark_baseline.cpp)

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
