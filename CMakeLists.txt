cmake_minimum_required(VERSION 3.20)
project(VPP VERSION 1.0.0 LANGUAGES NONE)

set(VPP_BOOTSTRAP_FULL "${CMAKE_SOURCE_DIR}/bootstrap/macos-arm64/vpp-native" CACHE FILEPATH "Autonomous V++ compiler seed")
set(VPP_BOOTSTRAP_I32 "${CMAKE_SOURCE_DIR}/bootstrap/macos-arm64/vpp-i32" CACHE FILEPATH "V++ i32 compiler seed")
if(NOT CMAKE_HOST_SYSTEM_NAME STREQUAL "Darwin" OR NOT CMAKE_HOST_SYSTEM_PROCESSOR MATCHES "^(arm64|aarch64)$")
  message(FATAL_ERROR "Quá trình tự biên dịch V++/ASM hoàn chỉnh hiện yêu cầu macOS ARM64")
endif()
file(GLOB_RECURSE VPP_SOURCES CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/gói/*.vi" "${CMAKE_SOURCE_DIR}/scripts/ASM/*")
set(VPP_BIN "${CMAKE_BINARY_DIR}/bin")
set(VPP_RUNTIME "${CMAKE_BINARY_DIR}/native-vpp-runtime")
add_custom_command(OUTPUT "${VPP_BIN}/vpp-native" "${VPP_BIN}/vpp" "${VPP_BIN}/vpp-i32"
  "${VPP_RUNTIME}/runtime.o" "${VPP_RUNTIME}/platform.o" "${VPP_RUNTIME}/dynamic-bridge.o"
  COMMAND /bin/bash "${CMAKE_SOURCE_DIR}/scripts/build-vpp-cli.sh" "${CMAKE_BINARY_DIR}" "${VPP_BOOTSTRAP_FULL}" "${VPP_BOOTSTRAP_I32}"
  DEPENDS ${VPP_SOURCES} "${VPP_BOOTSTRAP_FULL}" "${VPP_BOOTSTRAP_I32}"
    scripts/build-vpp-cli.sh scripts/build-vpp-aot-runtime.sh scripts/build-vpp-progress.sh scripts/vpp-native-launcher.sh.in bootstrap/macos-arm64/SHA256SUMS
  COMMENT "Khởi tạo bộ biên dịch V++ (xem các mốc tiến độ bên dưới)"
  VERBATIM USES_TERMINAL)
add_custom_target(vpp-cli ALL DEPENDS "${VPP_BIN}/vpp-native" "${VPP_BIN}/vpp" "${VPP_BIN}/vpp-i32"
  "${VPP_RUNTIME}/runtime.o" "${VPP_RUNTIME}/platform.o" "${VPP_RUNTIME}/dynamic-bridge.o")
include(CTest)
if(BUILD_TESTING)
  add_test(NAME vpp-source-boundary COMMAND /bin/sh "${CMAKE_SOURCE_DIR}/scripts/quality/audit_vi_asm_boundary.sh")
  set_tests_properties(vpp-source-boundary PROPERTIES ENVIRONMENT "VPP_AUDIT_BINARY=${VPP_BIN}/vpp-native")
  find_program(VPP_NODE node)
  if(VPP_NODE)
    add_test(NAME vpp-extension COMMAND "${VPP_NODE}" "${CMAKE_SOURCE_DIR}/scripts/quality/check_vpp_extension.js")
  endif()
  find_package(Python3 REQUIRED COMPONENTS Interpreter)
  set(VPP_REGRESSION "${CMAKE_SOURCE_DIR}/scripts/quality/run_native_regression.py")
  add_test(NAME vpp-regression-inventory COMMAND "${Python3_EXECUTABLE}" "${VPP_REGRESSION}" --inventory)
  set_tests_properties(vpp-source-boundary vpp-regression-inventory PROPERTIES LABELS smoke)
  if(VPP_NODE)
    set_tests_properties(vpp-extension PROPERTIES LABELS smoke)
  endif()
  add_test(NAME vpp-native-smoke COMMAND "${Python3_EXECUTABLE}" "${VPP_REGRESSION}"
    --suite smoke --compiler "${VPP_BIN}/vpp" --logs "${CMAKE_BINARY_DIR}/regression-logs/smoke")
  set_tests_properties(vpp-native-smoke PROPERTIES TIMEOUT 7200 LABELS smoke)
  add_test(NAME vpp-native-i32-regression COMMAND "${Python3_EXECUTABLE}" "${VPP_REGRESSION}"
    --suite i32 --compiler "${VPP_BIN}/vpp-i32" --logs "${CMAKE_BINARY_DIR}/regression-logs/i32")
  set_tests_properties(vpp-native-i32-regression PROPERTIES TIMEOUT 7200 LABELS "regression;i32")
  foreach(shard RANGE 0 15)
    add_test(NAME vpp-native-regression-${shard} COMMAND "${Python3_EXECUTABLE}" "${VPP_REGRESSION}"
      --compiler "${VPP_BIN}/vpp" --shard-count 16 --shard-index "${shard}"
      --logs "${CMAKE_BINARY_DIR}/regression-logs/full-${shard}")
    set_tests_properties(vpp-native-regression-${shard} PROPERTIES TIMEOUT 14400 LABELS "regression;full")
  endforeach()
endif()
install(PROGRAMS "${VPP_BIN}/vpp" "${VPP_BIN}/vpp-native" "${VPP_BIN}/vpp-i32" DESTINATION bin)
install(FILES "${VPP_RUNTIME}/runtime.o" "${VPP_RUNTIME}/platform.o" "${VPP_RUNTIME}/dynamic-bridge.o" DESTINATION share/vpp/native-vpp-runtime)
install(DIRECTORY "${CMAKE_SOURCE_DIR}/gói" DESTINATION share/vpp)
install(DIRECTORY "${CMAKE_SOURCE_DIR}/scripts/ASM" DESTINATION share/vpp/scripts
  FILES_MATCHING PATTERN "*.S" PATTERN "*.inc" PATTERN "*.md")
install(FILES "${CMAKE_SOURCE_DIR}/vpp-language.json" "${CMAKE_SOURCE_DIR}/vpp-libraries.json" DESTINATION share/vpp)
