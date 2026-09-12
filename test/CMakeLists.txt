add_executable(vpp-compiler-support-unit
    compiler_support_tests.cpp)

target_link_libraries(vpp-compiler-support-unit PRIVATE vpp-compiler)

add_test(NAME vpp-compiler-support-unit
         COMMAND vpp-compiler-support-unit)

add_executable(vpp-vm-opcode-smoke-unit
    vm_opcode_smoke_tests.cpp)

target_link_libraries(vpp-vm-opcode-smoke-unit PRIVATE vpp-runtime)

add_test(NAME vpp-vm-opcode-smoke-unit
         COMMAND vpp-vm-opcode-smoke-unit)

add_executable(vpp-runtime-value-unit
    runtime_value_tests.cpp)

target_link_libraries(vpp-runtime-value-unit PRIVATE vpp-runtime)

add_test(NAME vpp-runtime-value-unit
         COMMAND vpp-runtime-value-unit)

add_executable(vpp-opcode-and-native-constants-unit
    opcode_and_native_constants_tests.cpp)

target_link_libraries(vpp-opcode-and-native-constants-unit PRIVATE vpp-frontend)

add_test(NAME vpp-opcode-and-native-constants-unit
         COMMAND vpp-opcode-and-native-constants-unit)

add_executable(vpp-message-constants-unit
    message_constants_tests.cpp)

target_link_libraries(vpp-message-constants-unit PRIVATE vpp-core)

add_test(NAME vpp-message-constants-unit
         COMMAND vpp-message-constants-unit)

add_executable(vpp-pipeline-unit
    pipeline_tests.cpp)

target_link_libraries(vpp-pipeline-unit PRIVATE vpp-compiler)

add_test(NAME vpp-pipeline-unit
         COMMAND vpp-pipeline-unit)

add_executable(vpp-module-graph-unit
    module_graph_tests.cpp)

target_link_libraries(vpp-module-graph-unit PRIVATE vpp-compiler)

add_test(NAME vpp-module-graph-unit
         COMMAND vpp-module-graph-unit)

add_executable(vpp-recursive-ir-unit
    recursive_ir_tests.cpp)

target_link_libraries(vpp-recursive-ir-unit PRIVATE vpp-compiler)

add_test(NAME vpp-recursive-ir-unit
         COMMAND vpp-recursive-ir-unit)

add_executable(vpp-direct-codegen-unit
    direct_codegen_tests.cpp)

target_link_libraries(vpp-direct-codegen-unit PRIVATE vpp-compiler)

add_test(NAME vpp-direct-codegen-unit
         COMMAND vpp-direct-codegen-unit)

add_executable(vpp-pipeline-legacy-parity
    pipeline_legacy_parity_tests.cpp)

target_link_libraries(vpp-pipeline-legacy-parity PRIVATE vpp-compiler)

add_test(NAME vpp-pipeline-legacy-parity
         COMMAND vpp-pipeline-legacy-parity
                 ${CMAKE_SOURCE_DIR}
                 ${CMAKE_SOURCE_DIR}/test/data/legacy_compiler_snapshots.tsv)
set_tests_properties(vpp-pipeline-legacy-parity PROPERTIES
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

add_executable(vpp-tooling-unit
    tooling_tests.cpp)

target_link_libraries(vpp-tooling-unit PRIVATE vpp-tooling)

add_test(NAME vpp-tooling-unit
         COMMAND vpp-tooling-unit)

add_test(NAME vpp-cli-dump-ast
         COMMAND $<TARGET_FILE:vpp-cli>
                 --dump-ast
                 ${CMAKE_SOURCE_DIR}/src/tests/kiem_tra_tra_ve.vi)
set_tests_properties(vpp-cli-dump-ast PROPERTIES
    PASS_REGULAR_EXPRESSION "function")

add_test(NAME vpp-cli-dump-ir
         COMMAND $<TARGET_FILE:vpp-cli>
                 --dump-ir
                 ${CMAKE_SOURCE_DIR}/src/tests/kiem_tra_tra_ve.vi)
set_tests_properties(vpp-cli-dump-ir PROPERTIES
    PASS_REGULAR_EXPRESSION "define_function")

foreach(vpp_test_target IN ITEMS
        vpp-compiler-support-unit
        vpp-vm-opcode-smoke-unit
        vpp-runtime-value-unit
        vpp-opcode-and-native-constants-unit
        vpp-message-constants-unit
        vpp-pipeline-unit
        vpp-module-graph-unit
        vpp-recursive-ir-unit
        vpp-direct-codegen-unit
        vpp-pipeline-legacy-parity
        vpp-tooling-unit)
    target_compile_features(${vpp_test_target} PRIVATE cxx_std_17)
    if (MSVC)
        # Test sources also contain Vietnamese UTF-8 literals.
        target_compile_options(${vpp_test_target} PRIVATE /W4 /permissive- /utf-8)
    else()
        target_compile_options(${vpp_test_target} PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endforeach()
