add_executable(vpp-compiler-support-unit
    compiler_support_tests.cpp)

target_link_libraries(vpp-compiler-support-unit PRIVATE vpp-compiler Threads::Threads)

add_test(NAME vpp-compiler-support-unit
         COMMAND vpp-compiler-support-unit)

add_executable(vpp-vm-opcode-smoke-unit
    vm_opcode_smoke_tests.cpp)

target_link_libraries(vpp-vm-opcode-smoke-unit PRIVATE vpp-runtime)

add_test(NAME vpp-vm-opcode-smoke-unit
         COMMAND vpp-vm-opcode-smoke-unit)

add_executable(vpp-vm-handler-unit
    vm_handler_tests.cpp)

target_link_libraries(vpp-vm-handler-unit PRIVATE vpp-runtime)

add_test(NAME vpp-vm-handler-unit
         COMMAND vpp-vm-handler-unit)

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

add_executable(vpp-semver-unit
    semver_tests.cpp)

target_link_libraries(vpp-semver-unit PRIVATE vpp-core)

add_test(NAME vpp-semver-unit
         COMMAND vpp-semver-unit)

add_executable(vpp-package-manifest-unit
    package_manifest_tests.cpp)

target_link_libraries(vpp-package-manifest-unit PRIVATE vpp-core)

add_test(NAME vpp-package-manifest-unit
         COMMAND vpp-package-manifest-unit)

add_executable(vpp-package-solver-unit
    package_solver_tests.cpp)

target_link_libraries(vpp-package-solver-unit PRIVATE vpp-core)

add_test(NAME vpp-package-solver-unit
         COMMAND vpp-package-solver-unit)

add_executable(vpp-package-installer-unit
    package_installer_tests.cpp)

target_link_libraries(vpp-package-installer-unit PRIVATE vpp-core)

add_test(NAME vpp-package-installer-unit
         COMMAND vpp-package-installer-unit)

add_executable(vpp-package-cache-unit
    package_cache_tests.cpp)

target_link_libraries(vpp-package-cache-unit PRIVATE vpp-core)

add_test(NAME vpp-package-cache-unit
         COMMAND vpp-package-cache-unit)

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

add_executable(vpp-package-resolver-unit
    package_resolver_tests.cpp)

target_link_libraries(vpp-package-resolver-unit PRIVATE vpp-compiler)

add_test(NAME vpp-package-resolver-unit
         COMMAND vpp-package-resolver-unit)

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

add_executable(vpp-object-model-integration-unit
    object_model_integration_tests.cpp)

target_link_libraries(vpp-object-model-integration-unit PRIVATE vpp-compiler vpp-runtime)

add_test(NAME vpp-object-model-integration-unit
         COMMAND vpp-object-model-integration-unit)

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
    PASS_REGULAR_EXPRESSION "hàm")

add_test(NAME vpp-cli-dump-ir
         COMMAND $<TARGET_FILE:vpp-cli>
                 --dump-ir
                 ${CMAKE_SOURCE_DIR}/src/tests/kiem_tra_tra_ve.vi)
set_tests_properties(vpp-cli-dump-ir PROPERTIES
    PASS_REGULAR_EXPRESSION "định nghĩa hàm")

add_test(NAME vpp-package-cli-unit
         COMMAND ${CMAKE_COMMAND}
                 -DVPP_EXEC=$<TARGET_FILE:vpp-cli>
                 -DTEST_ROOT=${CMAKE_CURRENT_BINARY_DIR}/package-cli-case
                 -P ${CMAKE_CURRENT_SOURCE_DIR}/package_cli_tests.cmake)

foreach(vpp_test_target IN ITEMS
        vpp-compiler-support-unit
        vpp-vm-opcode-smoke-unit
        vpp-vm-handler-unit
        vpp-runtime-value-unit
        vpp-opcode-and-native-constants-unit
        vpp-message-constants-unit
        vpp-semver-unit
        vpp-package-manifest-unit
        vpp-package-solver-unit
        vpp-package-installer-unit
        vpp-package-cache-unit
        vpp-pipeline-unit
        vpp-module-graph-unit
        vpp-package-resolver-unit
        vpp-recursive-ir-unit
        vpp-direct-codegen-unit
        vpp-object-model-integration-unit
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
