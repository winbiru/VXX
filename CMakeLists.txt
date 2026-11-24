cmake_minimum_required(VERSION 3.20)
project(VPP VERSION 1.0.0 LANGUAGES NONE)

set(VPP_TLS_TEST_COMPILER "" CACHE FILEPATH "Existing V++ compiler for standalone TLS tests")
set(VPP_TLS_TEST_CLANG "" CACHE FILEPATH "Clang toolchain for Windows TLS tests")
option(VPP_TLS_INTEROP_TESTS "Run live TLS tests (requires Python cryptography)" OFF)

function(vpp_register_tls_tests compiler)
  set(runner "${CMAKE_SOURCE_DIR}/scripts/quality/check_vm_tls_vpp_interop.py")
  set(args --compiler "${compiler}" --logs "${CMAKE_BINARY_DIR}/tls-tests")
  if(CMAKE_HOST_WIN32)
    if(NOT VPP_TLS_TEST_CLANG)
      message(FATAL_ERROR "VPP_TLS_TEST_CLANG is required for Windows TLS tests")
    endif()
    list(APPEND args --clang "${VPP_TLS_TEST_CLANG}")
  endif()
  execute_process(COMMAND "${Python3_EXECUTABLE}" "${runner}" --list-fixtures
    OUTPUT_VARIABLE fixtures OUTPUT_STRIP_TRAILING_WHITESPACE COMMAND_ERROR_IS_FATAL ANY)
  foreach(fixture IN LISTS fixtures)
    add_test(NAME "vpp-tls-${fixture}" COMMAND "${Python3_EXECUTABLE}" "${runner}" ${args} --case "${fixture}")
    set_tests_properties("vpp-tls-${fixture}" PROPERTIES TIMEOUT 900 LABELS tls)
  endforeach()
  add_test(NAME vpp-tls-x509 COMMAND "${Python3_EXECUTABLE}" "${runner}" ${args} --case x509)
  set_tests_properties(vpp-tls-x509 PROPERTIES TIMEOUT 1530 LABELS tls RUN_SERIAL TRUE)
  if(VPP_TLS_INTEROP_TESTS)
    add_test(NAME vpp-tls-interop COMMAND "${Python3_EXECUTABLE}" "${runner}" ${args} --case interop)
    set_tests_properties(vpp-tls-interop PROPERTIES TIMEOUT 1200 LABELS tls RUN_SERIAL TRUE)
  endif()
endfunction()

# Test an existing compiler on any supported host without rebuilding the macOS bootstrap.
if(VPP_TLS_TEST_COMPILER)
  enable_testing()
  find_package(Python3 REQUIRED COMPONENTS Interpreter)
  vpp_register_tls_tests("${VPP_TLS_TEST_COMPILER}")
  return()
endif()

set(VPP_BOOTSTRAP_FULL "${CMAKE_SOURCE_DIR}/bootstrap/macos-arm64/vpp-native" CACHE FILEPATH "Autonomous V++ compiler seed")
set(VPP_BOOTSTRAP_I32 "${CMAKE_SOURCE_DIR}/bootstrap/macos-arm64/vpp-i32" CACHE FILEPATH "V++ i32 compiler seed")
set(VPP_PREBUILT_I32_RUNTIME "" CACHE PATH "Prepared macOS i32 compiler and bootstrap runtime directory")
option(VPP_CI_PINNED_FULL_STAGE1 "Use the audited pinned full seed for CI Stage 1; regenerate current sources in Stages 2/3" OFF)
if(VPP_CI_PINNED_FULL_STAGE1 AND NOT VPP_PREBUILT_I32_RUNTIME)
  message(FATAL_ERROR "VPP_CI_PINNED_FULL_STAGE1 requires VPP_PREBUILT_I32_RUNTIME")
endif()
set(VPP_PREBUILT_I32_INPUTS)
if(VPP_PREBUILT_I32_RUNTIME)
  foreach(part vpp-i32 runtime.o platform.o dynamic-bridge.o)
    list(APPEND VPP_PREBUILT_I32_INPUTS "${VPP_PREBUILT_I32_RUNTIME}/${part}")
  endforeach()
endif()
if(NOT CMAKE_HOST_SYSTEM_NAME STREQUAL "Darwin" OR NOT CMAKE_HOST_SYSTEM_PROCESSOR MATCHES "^(arm64|aarch64)$")
  message(FATAL_ERROR "Quá trình tự biên dịch V++/ASM hoàn chỉnh hiện yêu cầu macOS ARM64")
endif()
file(GLOB_RECURSE VPP_SOURCES CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/gói/*.vi" "${CMAKE_SOURCE_DIR}/scripts/ASM/*")
set(VPP_BIN "${CMAKE_BINARY_DIR}/bin")
set(VPP_RUNTIME "${CMAKE_BINARY_DIR}/native-vpp-runtime")
file(GENERATE OUTPUT "${CMAKE_BINARY_DIR}/vpp-bootstrap-mode.txt" CONTENT "${VPP_CI_PINNED_FULL_STAGE1}\n")
add_custom_command(OUTPUT "${VPP_BIN}/vpp-native" "${VPP_BIN}/vpp" "${VPP_BIN}/vpp-i32"
  "${VPP_RUNTIME}/runtime.o" "${VPP_RUNTIME}/platform.o" "${VPP_RUNTIME}/dynamic-bridge.o"
  COMMAND /bin/bash "${CMAKE_SOURCE_DIR}/scripts/macos/build-vpp-cli.sh" "${CMAKE_BINARY_DIR}" "${VPP_BOOTSTRAP_FULL}" "${VPP_BOOTSTRAP_I32}" "${VPP_PREBUILT_I32_RUNTIME}" "${VPP_CI_PINNED_FULL_STAGE1}"
  DEPENDS "${CMAKE_BINARY_DIR}/vpp-bootstrap-mode.txt" ${VPP_SOURCES} "${VPP_BOOTSTRAP_FULL}" "${VPP_BOOTSTRAP_I32}" ${VPP_PREBUILT_I32_INPUTS}
    scripts/macos/build-vpp-cli.sh scripts/build-vpp-aot-runtime.sh scripts/build-vpp-progress.sh scripts/macos/vpp-native-launcher.sh.in bootstrap/macos-arm64/SHA256SUMS bootstrap/macos-arm64/vpp-full.S.gz
  COMMENT "Khởi tạo bộ biên dịch V++ (xem các mốc tiến độ bên dưới)"
  VERBATIM USES_TERMINAL)
add_custom_target(vpp-cli ALL DEPENDS "${VPP_BIN}/vpp-native" "${VPP_BIN}/vpp" "${VPP_BIN}/vpp-i32"
  "${VPP_RUNTIME}/runtime.o" "${VPP_RUNTIME}/platform.o" "${VPP_RUNTIME}/dynamic-bridge.o")
add_custom_command(OUTPUT "${VPP_BIN}/vpp-vm"
  COMMAND /bin/bash "${CMAKE_SOURCE_DIR}/scripts/macos/build-vpp-vm.sh" "${VPP_BIN}/vpp-i32" "${VPP_BIN}/vpp-vm"
  DEPENDS "${VPP_BIN}/vpp-i32" ${VPP_SOURCES} scripts/macos/build-vpp-vm.sh
  COMMENT "Biên dịch máy ảo V++ dùng chung cho các nhóm kiểm thử"
  VERBATIM)
add_custom_target(vpp-vm ALL DEPENDS "${VPP_BIN}/vpp-vm")
# The bootstrap has multiple outputs shared by both targets. Make needs a
# target-level ordering edge as well as the compiler file dependency above,
# otherwise it can execute the same bootstrap rule in parallel target recipes.
add_dependencies(vpp-vm vpp-cli)
add_custom_command(OUTPUT "${VPP_BIN}/vpp-jit"
  COMMAND "${VPP_BIN}/vpp" build "${CMAKE_SOURCE_DIR}/gói/biên dịch/dòng lệnh JIT.vi" -o "${VPP_BIN}/vpp-jit"
  DEPENDS "${VPP_BIN}/vpp" "${VPP_BIN}/vpp-native" ${VPP_SOURCES}
  COMMENT "Biên dịch bộ JIT V++ chạy mã máy trong bộ nhớ"
  VERBATIM USES_TERMINAL)
if(VPP_CI_PINNED_FULL_STAGE1)
  add_custom_target(vpp-jit DEPENDS "${VPP_BIN}/vpp-jit")
else()
  add_custom_target(vpp-jit ALL DEPENDS "${VPP_BIN}/vpp-jit")
endif()
add_dependencies(vpp-jit vpp-cli)
add_custom_command(OUTPUT "${VPP_BIN}/vpp-tools"
  COMMAND "${VPP_BIN}/vpp" build "${CMAKE_SOURCE_DIR}/scripts/vdk/vpp-cli.vi" -o "${VPP_BIN}/vpp-tools"
  DEPENDS "${VPP_BIN}/vpp" "${VPP_BIN}/vpp-native" ${VPP_SOURCES} scripts/vdk/vpp-cli.vi
  COMMENT "Biên dịch công cụ dự án V++"
  VERBATIM USES_TERMINAL)
add_custom_target(vpp-tools ALL DEPENDS "${VPP_BIN}/vpp-tools")
add_dependencies(vpp-tools vpp-cli)
# Trợ giúp cửa sổ riêng biệt; chỉ ứng dụng đồ họa mới khởi chạy nó.
find_program(VPP_GRAPHICS_CLANG clang REQUIRED)
add_custom_command(OUTPUT "${VPP_BIN}/vpp-graphics-macos"
  COMMAND "${CMAKE_COMMAND}" -E make_directory "${VPP_BIN}"
  COMMAND "${VPP_GRAPHICS_CLANG}" -arch arm64
    "${CMAKE_SOURCE_DIR}/scripts/ASM/macos/vpp_graphics_arm64.S"
    -framework Cocoa -framework WebKit -o "${VPP_BIN}/vpp-graphics-macos"
  DEPENDS scripts/ASM/macos/vpp_graphics_arm64.S
  VERBATIM)
add_custom_command(OUTPUT "${VPP_BIN}/vpp-graphics-metal"
  COMMAND "${CMAKE_COMMAND}" -E make_directory "${VPP_BIN}"
  COMMAND "${VPP_GRAPHICS_CLANG}" -arch arm64
    "${CMAKE_SOURCE_DIR}/scripts/ASM/macos/vpp_metal_arm64.S"
    -framework Metal -framework Foundation -o "${VPP_BIN}/vpp-graphics-metal"
  DEPENDS scripts/ASM/macos/vpp_metal_arm64.S
  VERBATIM)
add_custom_target(vpp-graphics ALL DEPENDS "${VPP_BIN}/vpp-graphics-macos" "${VPP_BIN}/vpp-graphics-metal")
install(PROGRAMS "${VPP_BIN}/vpp-graphics-macos" "${VPP_BIN}/vpp-graphics-metal" DESTINATION bin)
include(CTest)
option(VPP_GRAPHICS_GPU_TESTS "Kiểm tra GPU thực tế" OFF)
if(BUILD_TESTING)
  if(VPP_GRAPHICS_GPU_TESTS)
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    add_test(NAME vpp-graphics-metal COMMAND "${Python3_EXECUTABLE}"
      "${CMAKE_SOURCE_DIR}/scripts/graphics/check_gpu.py" "${VPP_BIN}/vpp-graphics-metal" --metal)
    set_tests_properties(vpp-graphics-metal PROPERTIES TIMEOUT 60)
  endif()
  add_test(NAME vpp-graphics-helper COMMAND "${VPP_BIN}/vpp-graphics-macos" --check)
  set_tests_properties(vpp-graphics-helper PROPERTIES LABELS smoke)
  add_test(NAME vpp-source-boundary COMMAND /bin/sh "${CMAKE_SOURCE_DIR}/scripts/quality/audit_vi_asm_boundary.sh")
  set_tests_properties(vpp-source-boundary PROPERTIES ENVIRONMENT "VPP_AUDIT_BINARY=${VPP_BIN}/vpp-native")
  find_program(VPP_NODE node)
  if(VPP_NODE)
    add_test(NAME vpp-extension COMMAND "${VPP_NODE}" "${CMAKE_SOURCE_DIR}/scripts/quality/check_vpp_extension.js")
  endif()
  find_package(Python3 REQUIRED COMPONENTS Interpreter)
  vpp_register_tls_tests("${VPP_BIN}/vpp")
  if(APPLE AND CMAKE_SYSTEM_PROCESSOR MATCHES "^(arm64|aarch64)$")
    add_test(NAME vpp-native-tls COMMAND "${Python3_EXECUTABLE}"
      "${CMAKE_SOURCE_DIR}/scripts/quality/check_vm_native_tls_macos.py")
    set_tests_properties(vpp-native-tls PROPERTIES TIMEOUT 60 LABELS "smoke;tls")
    add_test(NAME vpp-worker-machine COMMAND "${Python3_EXECUTABLE}"
      "${CMAKE_SOURCE_DIR}/scripts/quality/check_worker_machine.py")
    set_tests_properties(vpp-worker-machine PROPERTIES TIMEOUT 120 LABELS "smoke;worker")
    add_test(NAME vpp-compiler-workers COMMAND "${CMAKE_COMMAND}"
      "-DVPP_ROOT=${CMAKE_SOURCE_DIR}" "-DVPP_NATIVE_COMPILER=${VPP_BIN}/vpp"
      "-DVPP_VM=${VPP_BIN}/vpp-vm" "-DVPP_OUTPUT_DIR=${CMAKE_BINARY_DIR}/compiler-workers"
      -P "${CMAKE_SOURCE_DIR}/scripts/quality/check_compiler_workers.cmake")
    set_tests_properties(vpp-compiler-workers PROPERTIES TIMEOUT 7200 LABELS "smoke;worker")
  endif()
  foreach(mode format diagnostics)
    add_test(NAME vpp-lsp-${mode} COMMAND "${CMAKE_COMMAND}"
      "-DVPP_CLI=${VPP_BIN}/vpp" "-DVPP_NATIVE_COMPILER=${VPP_BIN}/vpp-native"
      "-DVPP_ROOT=${CMAKE_SOURCE_DIR}" "-DVPP_OUTPUT_DIR=${CMAKE_BINARY_DIR}/lsp-${mode}"
      "-DVPP_LSP_MODE=${mode}" -P "${CMAKE_SOURCE_DIR}/scripts/quality/check_native_lsp.cmake")
    set_tests_properties(vpp-lsp-${mode} PROPERTIES TIMEOUT 180 LABELS "smoke;lsp")
  endforeach()
  add_test(NAME vpp-lsp-catalog COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_SOURCE_DIR}/scripts/quality/check_native_lsp_catalog.py"
    "${VPP_BIN}/vpp-tools" "${VPP_BIN}/vpp-native" "${CMAKE_SOURCE_DIR}")
  set_tests_properties(vpp-lsp-catalog PROPERTIES TIMEOUT 180 LABELS "smoke;lsp")
  add_test(NAME vpp-lsp-semantic COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_SOURCE_DIR}/scripts/quality/check_native_lsp_semantic.py"
    "${VPP_BIN}/vpp-tools" "${VPP_BIN}/vpp-native" "${CMAKE_SOURCE_DIR}")
  set_tests_properties(vpp-lsp-semantic PROPERTIES TIMEOUT 360 LABELS "smoke;lsp")
  add_test(NAME vpp-package-manager COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_SOURCE_DIR}/scripts/quality/check_native_package_manager.py"
    "${VPP_BIN}/vpp-tools" "${VPP_BIN}/vpp-native")
  set_tests_properties(vpp-package-manager PROPERTIES TIMEOUT 300 LABELS "smoke;package")
  add_test(NAME vpp-jit-cli COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_SOURCE_DIR}/scripts/quality/check_jit_cli.py" "${VPP_BIN}/vpp-jit")
  add_test(NAME vpp-jit-dynamic COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_SOURCE_DIR}/scripts/quality/check_jit_dynamic.py" "${VPP_BIN}/vpp-jit" "${CMAKE_SOURCE_DIR}")
  set_tests_properties(vpp-jit-cli PROPERTIES TIMEOUT 300 LABELS jit)
  set_tests_properties(vpp-jit-dynamic PROPERTIES TIMEOUT 7200 LABELS jit)
  add_test(NAME vpp-opcode-abi COMMAND "${CMAKE_COMMAND}" "-DVPP_ROOT=${CMAKE_SOURCE_DIR}"
    -P "${CMAKE_SOURCE_DIR}/scripts/quality/check_vpp_opcodes.cmake")
  add_test(NAME vpp-script-configuration COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_SOURCE_DIR}/scripts/quality/test_script_configuration.py")
  set_tests_properties(vpp-opcode-abi vpp-script-configuration PROPERTIES LABELS smoke)
  set_tests_properties(vpp-script-configuration PROPERTIES TIMEOUT 60)
  set(VPP_REGRESSION "${CMAKE_SOURCE_DIR}/scripts/quality/run_native_regression.py")
  add_test(NAME vpp-regression-inventory COMMAND "${Python3_EXECUTABLE}" "${VPP_REGRESSION}" --inventory)
  set_tests_properties(vpp-source-boundary vpp-regression-inventory PROPERTIES LABELS smoke)
  if(VPP_NODE)
    set_tests_properties(vpp-extension PROPERTIES LABELS smoke)
  endif()
  add_test(NAME vpp-native-smoke COMMAND "${Python3_EXECUTABLE}" "${VPP_REGRESSION}"
    --suite smoke --backend jit --jit "${VPP_BIN}/vpp-jit" --logs "${CMAKE_BINARY_DIR}/regression-logs/smoke")
  set_tests_properties(vpp-native-smoke PROPERTIES TIMEOUT 7200 LABELS smoke)
  add_test(NAME vpp-native-regression COMMAND "${Python3_EXECUTABLE}" "${VPP_REGRESSION}"
    --suite full --backend jit --jit "${VPP_BIN}/vpp-jit" --jobs 2 --timeout 900 --interpret-reviewed
    --logs "${CMAKE_BINARY_DIR}/regression-logs/full")
  set_tests_properties(vpp-native-regression PROPERTIES TIMEOUT 21600 LABELS "regression;full;jit")
  add_test(NAME vpp-trusted-host-regression COMMAND "${Python3_EXECUTABLE}" "${VPP_REGRESSION}"
    --suite host --backend aot --compiler "${VPP_BIN}/vpp" --timeout 900
    --logs "${CMAKE_BINARY_DIR}/regression-logs/host")
  set_tests_properties(vpp-trusted-host-regression PROPERTIES TIMEOUT 1800 LABELS "regression;full;host")
endif()
install(PROGRAMS "${VPP_BIN}/vpp" "${VPP_BIN}/vpp-native" "${VPP_BIN}/vpp-i32" "${VPP_BIN}/vpp-vm" "${VPP_BIN}/vpp-jit" "${VPP_BIN}/vpp-tools" DESTINATION bin)
install(FILES "${VPP_RUNTIME}/runtime.o" "${VPP_RUNTIME}/platform.o" "${VPP_RUNTIME}/dynamic-bridge.o" DESTINATION share/vpp/native-vpp-runtime)
install(DIRECTORY "${CMAKE_SOURCE_DIR}/gói" "${CMAKE_SOURCE_DIR}/templates" DESTINATION share/vpp)
install(DIRECTORY "${CMAKE_SOURCE_DIR}/scripts/ASM" DESTINATION share/vpp/scripts
  FILES_MATCHING PATTERN "*.S" PATTERN "*.inc" PATTERN "*.md")
install(DIRECTORY "${CMAKE_SOURCE_DIR}/scripts/graphics" DESTINATION share/vpp/scripts
  FILES_MATCHING PATTERN "CMakeLists.txt" PATTERN "*.py")
install(FILES "${CMAKE_SOURCE_DIR}/vpp-language.json" "${CMAKE_SOURCE_DIR}/vpp-libraries.json" DESTINATION share/vpp)

# Record only files installed by CMake, never unrelated files in a shared prefix.
install(PROGRAMS "${CMAKE_SOURCE_DIR}/scripts/vdk/uninstall-native.sh" DESTINATION bin)
install(SCRIPT "${CMAKE_SOURCE_DIR}/scripts/vdk/install-manifest.cmake")

# Explicit packaging target: keep the bootstrap graph independent of release tooling.
find_package(Python3 QUIET COMPONENTS Interpreter)
if(Python3_Interpreter_FOUND)
  add_custom_target(vpp-release-packages
    COMMAND "${CMAKE_COMMAND}" --install "${CMAKE_BINARY_DIR}" --prefix "${CMAKE_BINARY_DIR}/release-prefix"
    COMMAND "${CMAKE_BINARY_DIR}/release-prefix/bin/vpp" vm-build "${CMAKE_BINARY_DIR}/release-prefix/share/vpp/gói/máy ảo/nhà cung cấp DNS.vi" -o "${CMAKE_BINARY_DIR}/release-prefix/bin/dns-provider.vmb"
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/scripts/quality/package_release.py" --platform macos-arm64 --prefix "${CMAKE_BINARY_DIR}/release-prefix" --output "${CMAKE_BINARY_DIR}/release-candidates"
    DEPENDS vpp-cli vpp-vm vpp-jit vpp-tools vpp-graphics
    COMMENT "Đóng gói VPP và VDK từ compiler hiện tại"
    VERBATIM USES_TERMINAL)
endif()
