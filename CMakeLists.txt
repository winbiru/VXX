cmake_minimum_required(VERSION 3.20)
project(VPP VERSION 1.0.0 LANGUAGES NONE)

set(VPP_BOOTSTRAP_FULL "${CMAKE_SOURCE_DIR}/bootstrap/macos-arm64/vpp-native" CACHE FILEPATH "Autonomous V++ compiler seed")
set(VPP_BOOTSTRAP_I32 "${CMAKE_SOURCE_DIR}/bootstrap/macos-arm64/vpp-i32" CACHE FILEPATH "V++ i32 compiler seed")
set(VPP_PREBUILT_I32_RUNTIME "" CACHE PATH "Prepared macOS i32 compiler and bootstrap runtime directory")
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
add_custom_command(OUTPUT "${VPP_BIN}/vpp-native" "${VPP_BIN}/vpp" "${VPP_BIN}/vpp-i32"
  "${VPP_RUNTIME}/runtime.o" "${VPP_RUNTIME}/platform.o" "${VPP_RUNTIME}/dynamic-bridge.o"
  COMMAND /bin/bash "${CMAKE_SOURCE_DIR}/scripts/build-vpp-cli.sh" "${CMAKE_BINARY_DIR}" "${VPP_BOOTSTRAP_FULL}" "${VPP_BOOTSTRAP_I32}" "${VPP_PREBUILT_I32_RUNTIME}"
  DEPENDS ${VPP_SOURCES} "${VPP_BOOTSTRAP_FULL}" "${VPP_BOOTSTRAP_I32}" ${VPP_PREBUILT_I32_INPUTS}
    scripts/build-vpp-cli.sh scripts/build-vpp-aot-runtime.sh scripts/build-vpp-progress.sh scripts/vpp-native-launcher.sh.in bootstrap/macos-arm64/SHA256SUMS
  COMMENT "Khởi tạo bộ biên dịch V++ (xem các mốc tiến độ bên dưới)"
  VERBATIM USES_TERMINAL)
add_custom_target(vpp-cli ALL DEPENDS "${VPP_BIN}/vpp-native" "${VPP_BIN}/vpp" "${VPP_BIN}/vpp-i32"
  "${VPP_RUNTIME}/runtime.o" "${VPP_RUNTIME}/platform.o" "${VPP_RUNTIME}/dynamic-bridge.o")
add_custom_command(OUTPUT "${VPP_BIN}/vpp-vm"
  COMMAND "${CMAKE_COMMAND}" -E env "VPP_HOME=${CMAKE_SOURCE_DIR}" "VPP_NATIVE_NO_CACHE=1"
    "${VPP_BIN}/vpp-i32" build "${CMAKE_SOURCE_DIR}/gói/máy ảo/chính.vi" -o "${VPP_BIN}/vpp-vm"
  DEPENDS "${VPP_BIN}/vpp-i32" ${VPP_SOURCES}
  COMMENT "Biên dịch máy ảo V++ dùng chung cho các nhóm kiểm thử"
  VERBATIM)
add_custom_target(vpp-vm ALL DEPENDS "${VPP_BIN}/vpp-vm")
# The bootstrap has multiple outputs shared by both targets. Make needs a
# target-level ordering edge as well as the compiler file dependency above,
# otherwise it can execute the same bootstrap rule in parallel target recipes.
add_dependencies(vpp-vm vpp-cli)
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
  add_test(NAME vpp-regression-runner COMMAND "${Python3_EXECUTABLE}" -m unittest discover
    -s "${CMAKE_SOURCE_DIR}/scripts/quality" -p test_run_native_regression.py)
  add_test(NAME vpp-build-graph COMMAND "${Python3_EXECUTABLE}" -m unittest discover
    -s "${CMAKE_SOURCE_DIR}/scripts/quality" -p test_cmake_build_graph.py)
  set_tests_properties(vpp-source-boundary vpp-regression-inventory vpp-regression-runner vpp-build-graph PROPERTIES LABELS smoke)
  set_tests_properties(vpp-build-graph PROPERTIES TIMEOUT 60)
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
install(PROGRAMS "${VPP_BIN}/vpp" "${VPP_BIN}/vpp-native" "${VPP_BIN}/vpp-i32" "${VPP_BIN}/vpp-vm" DESTINATION bin)
install(FILES "${VPP_RUNTIME}/runtime.o" "${VPP_RUNTIME}/platform.o" "${VPP_RUNTIME}/dynamic-bridge.o" DESTINATION share/vpp/native-vpp-runtime)
install(DIRECTORY "${CMAKE_SOURCE_DIR}/gói" DESTINATION share/vpp)
install(DIRECTORY "${CMAKE_SOURCE_DIR}/scripts/ASM" DESTINATION share/vpp/scripts
  FILES_MATCHING PATTERN "*.S" PATTERN "*.inc" PATTERN "*.md")
install(FILES "${CMAKE_SOURCE_DIR}/vpp-language.json" "${CMAKE_SOURCE_DIR}/vpp-libraries.json" DESTINATION share/vpp)
