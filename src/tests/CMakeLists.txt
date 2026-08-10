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

foreach(vpp_test_target IN ITEMS
        vpp-compiler-support-unit
        vpp-vm-opcode-smoke-unit
        vpp-opcode-and-native-constants-unit
        vpp-message-constants-unit)
    target_compile_features(${vpp_test_target} PRIVATE cxx_std_17)
    if (MSVC)
        # Test sources also contain Vietnamese UTF-8 literals.
        target_compile_options(${vpp_test_target} PRIVATE /W4 /permissive- /utf-8)
    else()
        target_compile_options(${vpp_test_target} PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endforeach()
