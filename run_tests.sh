#!/usr/bin/env bash
set -u

ROOT_DIR="$(pwd)"
if [ -n "${VPP_EXEC:-}" ]; then
  EXEC="$VPP_EXEC"
elif [ -x "bin/vpp-cli" ]; then
  EXEC="bin/vpp-cli"
elif [ -x "cmake-build-debug/bin/vpp-cli" ]; then
  EXEC="cmake-build-debug/bin/vpp-cli"
elif [ -x "cmake-build-debug/bin/vietvm-cli" ]; then
  EXEC="cmake-build-debug/bin/vietvm-cli"
else
  echo "ERROR: khong tim thay vpp-cli; hay build truoc hoac dat VPP_EXEC" >&2
  exit 2
fi

case "$EXEC" in
  /*) EXEC_PATH="$EXEC" ;;
  *) EXEC_PATH="$ROOT_DIR/$EXEC" ;;
esac

if [ ! -x "$EXEC_PATH" ]; then
  echo "ERROR: VPP executable khong chay duoc: $EXEC_PATH" >&2
  exit 2
fi
tmpdir="src/tests/.tmp"
mkdir -p "$tmpdir"
PASS=0; FAIL=0
HTTP_FILE_TRANSPORT_DIR=""

# HTTP tests prefer real localhost TCP. If the execution environment denies
# bind(2), the runtime can use a file-backed IPC transport while preserving the
# same V++ HTTP client/server API across separate processes.
HTTP_FIXTURE_PID=""
EXAMPLE_PID=""
VPP_HOME_TEST_DIR=""
cleanup() {
  if [ -n "$EXAMPLE_PID" ]; then
    kill "$EXAMPLE_PID" 2>/dev/null || true
    wait "$EXAMPLE_PID" 2>/dev/null || true
  fi
  if [ -n "$HTTP_FIXTURE_PID" ]; then
    kill "$HTTP_FIXTURE_PID" 2>/dev/null || true
    wait "$HTTP_FIXTURE_PID" 2>/dev/null || true
  fi
  if [ -n "$VPP_HOME_TEST_DIR" ] && [ -d "$VPP_HOME_TEST_DIR" ]; then
    rm -rf "$VPP_HOME_TEST_DIR"
  fi
  if [ -n "$HTTP_FILE_TRANSPORT_DIR" ] && [ -d "$HTTP_FILE_TRANSPORT_DIR" ]; then
    rm -rf "$HTTP_FILE_TRANSPORT_DIR"
  fi
  rm -rf "$tmpdir"
}
trap cleanup EXIT INT TERM

http_get() {
  local url="$1"
  local output="$2"
  if [ -n "${VPP_HTTP_FILE_TRANSPORT_DIR:-}" ]; then
    cat >"$tmpdir/http_probe.vi" <<EOF
nhập gói/chuẩn/main.vi;

hàm main() {
    in mạng lấy("$url");
};
EOF
    if ! "$EXEC_PATH" "$tmpdir/http_probe.vi" >"$tmpdir/http_probe.raw" 2>&1; then
      return 1
    fi
    sed -n 's/^\[IN\] //p' "$tmpdir/http_probe.raw" >"$output"
    return 0
  fi
  curl -fsS --max-time 1 "$url" >"$output" 2>/dev/null
}

wait_http_value() {
  local url="$1"
  local expected="$2"
  local output="$3"
  local attempts=50
  if [ -n "${VPP_HTTP_FILE_TRANSPORT_DIR:-}" ]; then
    attempts=1
  fi
  for _ in $(seq 1 "$attempts"); do
    if http_get "$url" "$output" && [ "$(cat "$output")" = "$expected" ]; then
      return 0
    fi
    sleep 0.1
  done
  return 1
}

"$EXEC_PATH" src/tests/http_fixture.vi >"$tmpdir/http_fixture.log" 2>&1 &
HTTP_FIXTURE_PID=$!
if ! wait_http_value "http://127.0.0.1:18080/health" '{"ok":true}' "$tmpdir/http_fixture.output"; then
  if grep -Eq '\(mã=(1|13|10013)\)' "$tmpdir/http_fixture.log"; then
    kill "$HTTP_FIXTURE_PID" 2>/dev/null || true
    wait "$HTTP_FIXTURE_PID" 2>/dev/null || true
    HTTP_FIXTURE_PID=""

    HTTP_FILE_TRANSPORT_DIR=$(mktemp -d "$tmpdir/http-transport.XXXXXX")
    export VPP_HTTP_FILE_TRANSPORT_DIR="$ROOT_DIR/$HTTP_FILE_TRANSPORT_DIR"
    echo "INFO: localhost bind is restricted; using V++ HTTP file transport for integration tests"

    "$EXEC_PATH" src/tests/http_fixture.vi >"$tmpdir/http_fixture.log" 2>&1 &
    HTTP_FIXTURE_PID=$!
    if ! wait_http_value "http://127.0.0.1:18080/health" '{"ok":true}' "$tmpdir/http_fixture.output"; then
      echo "ERROR: HTTP fixture failed through the fallback transport" >&2
      cat "$tmpdir/http_fixture.log" >&2
      cat "$tmpdir/http_probe.raw" >&2 2>/dev/null || true
      exit 2
    fi
  else
    echo "ERROR: local HTTP test fixture did not return the expected /health response" >&2
    cat "$tmpdir/http_fixture.log" >&2
    exit 2
  fi
fi

echo "== Running examples/api_project/application.vi [example] =="
"$EXEC_PATH" examples/api_project/application.vi >"$tmpdir/api_example.log" 2>&1 &
EXAMPLE_PID=$!
if wait_http_value "http://127.0.0.1:8080/health" "true" "$tmpdir/api_example.output"; then
  echo "PASS: examples/api_project/application.vi [example]"; PASS=$((PASS+1))
else
  echo "FAIL: examples/api_project/application.vi [example]"; FAIL=$((FAIL+1))
  cat "$tmpdir/api_example.log" >&2
  cat "$tmpdir/http_probe.raw" >&2 2>/dev/null || true
fi
kill "$EXAMPLE_PID" 2>/dev/null || true
wait "$EXAMPLE_PID" 2>/dev/null || true
EXAMPLE_PID=""

echo "== Running backend scaffold smoke test =="
scaffold_dir=$(mktemp -d "$tmpdir/backend.XXXXXX")
scaffold_ok=0
scaffold_port=18081
if (
  cd "$scaffold_dir" &&
  VPP_HOME="$ROOT_DIR" "$EXEC_PATH" khởi tạo backend demo-api &&
  test -f demo-api/application.vi &&
  test -f demo-api/config.vi &&
  test -f demo-api/controller.vi &&
  test -f demo-api/router.vi &&
  test -f demo-api/server.vi &&
  test -f demo-api/application.properties &&
  test -f demo-api/README.md &&
  test -f demo-api/.gitignore &&
  sed -i.bak "s/^port=8080$/port=$scaffold_port/" demo-api/application.properties &&
  rm -f demo-api/application.properties.bak
); then
  (
    cd "$scaffold_dir/demo-api" &&
    VPP_HOME="$ROOT_DIR" "$EXEC_PATH" application.vi >"$ROOT_DIR/$tmpdir/backend_scaffold.log" 2>&1
  ) &
  EXAMPLE_PID=$!
  if wait_http_value "http://127.0.0.1:${scaffold_port}/health" "true" "$tmpdir/backend_scaffold.output"; then
    scaffold_ok=1
  fi
  kill "$EXAMPLE_PID" 2>/dev/null || true
  wait "$EXAMPLE_PID" 2>/dev/null || true
  EXAMPLE_PID=""
fi
if [ "$scaffold_ok" -eq 1 ]; then
  echo "PASS: backend scaffold"; PASS=$((PASS+1))
else
  echo "FAIL: backend scaffold"; FAIL=$((FAIL+1))
  cat "$tmpdir/backend_scaffold.log" >&2 2>/dev/null || true
  cat "$tmpdir/http_probe.raw" >&2 2>/dev/null || true
fi

# Verify bare Vietnamese package imports from a directory outside the repository.
# This exercises the installed-layout lookup: $VPP_HOME/gói/chuẩn/<tên package>/main.vi.
echo "== Running Vietnamese package import via VPP_HOME =="
VPP_HOME_TEST_DIR=$(mktemp -d "${TMPDIR:-/tmp}/vpp-package-import.XXXXXX")
vpp_home_out="$tmpdir/kiem_tra_package_tieng_viet_vpp_home.output"
if cp src/tests/kiem_tra_package_tieng_viet.vi "$VPP_HOME_TEST_DIR/kiem_tra_package_tieng_viet.vi" &&
  cp -R "$ROOT_DIR/gói" "$VPP_HOME_TEST_DIR/gói" &&
  (
    cd "$VPP_HOME_TEST_DIR" &&
    VPP_HOME="$VPP_HOME_TEST_DIR" "$EXEC_PATH" kiem_tra_package_tieng_viet.vi >"$ROOT_DIR/$vpp_home_out" 2>&1
  ) &&
  diff -u src/tests/expected/kiem_tra_package_tieng_viet.expected "$vpp_home_out"; then
  echo "PASS: Vietnamese package import via VPP_HOME"; PASS=$((PASS+1))
else
  echo "FAIL: Vietnamese package import via VPP_HOME"; FAIL=$((FAIL+1))
fi

TESTS=(
  src/tests/import_main.vi
  src/tests/kiem_tra_boolean.vi
  src/tests/kiem_tra_bo_qua.vi
  src/tests/kiem_tra_chon_ca.vi
  src/tests/kiem_tra_chuoi_co_ban.vi
  src/tests/kiem_tra_de_quy.vi
  src/tests/kiem_tra_file_dem.vi
  src/tests/kiem_tra_ngoai_le.vi
  src/tests/kiem_tra_ngoai_le_xuyen_ham.vi
  src/tests/kiem_tra_dieu_kien_long_nhieu_cap.vi
  src/tests/kiem_tra_dieu_kien_phu_dinh.vi
  src/tests/kiem_tra_ham.vi
  src/tests/kiem_tra_ham_4_tham_so.vi
  src/tests/kiem_tra_ham_tham_so.vi
  src/tests/kiem_tra_ham_da_tu_khong_nhay.vi
  src/tests/kiem_tra_mang_3_chieu.vi
  src/tests/kiem_tra_noi_chuoi.vi
  src/tests/kiem_tra_so_chan_1-20.vi
  src/tests/kiem_tra_so_chia_het_cho_3_va_4.vi
  src/tests/kiem_tra_so_le_chia_het_cho_5.vi
  src/tests/kiem_tra_so_nguyen.vi
  src/tests/kiem_tra_so_thuc.vi
  src/tests/kiem_tra_tong_hop_khong_xung_dot.vi
  src/tests/kiem_tra_rong_va_map.vi
  src/tests/kiem_tra_namespace_module.vi
  src/tests/kiem_tra_module_lifecycle.vi
  src/tests/kiem_tra_module_khoi_tao_mot_lan.vi
  src/tests/kiem_tra_module_alias_lifecycle.vi
  src/tests/kiem_tra_module_reexport.vi
  src/tests/kiem_tra_package_modules.vi
  src/tests/kiem_tra_package_09.vi
  src/tests/kiem_tra_package_tieng_viet.vi
  src/tests/kiem_tra_stdlib.vi
  src/tests/kiem_tra_stdlib_starter.vi
  src/tests/kiem_tra_stdlib_http.vi
  src/tests/kiem_tra_stdlib_http_post_put.vi
  src/tests/kiem_tra_application_server.vi
  src/tests/kiem_tra_rest_json_jwt.vi
  src/tests/kiem_tra_stdlib_tinh_toan.vi
  src/tests/kiem_tra_stdlib_mo_rong.vi
  src/tests/kiem_tra_goi_kiem_thu.vi
  src/tests/kiem_tra_goi_mang.vi
  src/tests/kiem_tra_stdlib_nen_tang.vi
  src/tests/kiem_tra_json_phan_tich.vi
  src/tests/kiem_tra_json_an_toan.vi
  src/tests/kiem_tra_stdlib_io_config_time.vi
  src/tests/kiem_tra_api_thuc_thu.vi
  src/tests/kiem_tra_api_db_project.vi
  src/tests/kiem_tra_goi_dung.vi
  src/tests/kiem_tra_nhat_ky.vi
  src/tests/kiem_tra_lambda_hof_mac_dinh.vi
  src/tests/kiem_tra_closure_capture.vi
  src/tests/kiem_tra_list_literal.vi
  src/tests/kiem_tra_collection_bai_63_75.vi
  src/tests/kiem_tra_toan_tu_moi.vi
  src/tests/kiem_tra_lop_truy_cap.vi
  src/tests/kiem_tra_object_model.vi
  src/tests/kiem_tra_constructor_tham_so.vi
  src/tests/kiem_tra_visibility_instance.vi
  src/tests/kiem_tra_receiver_minh.vi
  src/tests/kiem_tra_receiver_minh_dieu_khien.vi
  src/tests/kiem_tra_receiver_minh_nhieu_instance.vi
  src/tests/kiem_tra_receiver_minh_de_quy.vi
  src/tests/kiem_tra_ke_thua.vi
  src/tests/kiem_tra_giao_dien_trien_khai.vi
  src/tests/kiem_tra_semantics_gia_tri.vi
  src/tests/kiem_tra_kieu_dong_call_boundary.vi
  src/tests/kiem_tra_hoi_quy_tong_hop.vi
  src/tests/kiem_tra_cu_phap_modifier_cu.vi
  src/tests/kiem_tra_tra_ve.vi
  src/tests/program.vi
)

for testfile in "${TESTS[@]}"; do
  base=$(basename "${testfile%.vi}")
  out="$tmpdir/$base.output"
  exp="src/tests/expected/$base.expected"
  echo "== Running $testfile =="
  "$EXEC_PATH" "$testfile" > "$out" 2>&1
  test_status=$?
  if [ -f "$exp" ]; then
    if [ "$test_status" -ne 0 ]; then
      echo "FAIL: $testfile (exit code: $test_status)"; FAIL=$((FAIL+1))
      cat "$out" >&2
    elif diff -u "$exp" "$out"; then
      echo "PASS: $testfile"; PASS=$((PASS+1))
    else
      echo "FAIL: $testfile"; FAIL=$((FAIL+1))
    fi
  else
    echo "NO EXPECTED: $testfile"; FAIL=$((FAIL+1))
  fi
done

echo "== Running src/tests/kiem_tra_de_quy_vuot_gioi_han.vi [expected runtime failure] =="
call_depth_test="src/tests/kiem_tra_de_quy_vuot_gioi_han.vi"
call_depth_out="$tmpdir/kiem_tra_de_quy_vuot_gioi_han.output"
call_depth_exp="src/tests/expected/kiem_tra_de_quy_vuot_gioi_han.expected"
"$EXEC_PATH" "$call_depth_test" > "$call_depth_out" 2>&1
call_depth_status=$?
if [ "$call_depth_status" -eq 0 ]; then
  echo "FAIL: $call_depth_test (expected non-zero exit code)"; FAIL=$((FAIL+1))
elif diff -u "$call_depth_exp" "$call_depth_out"; then
  echo "PASS: $call_depth_test"; PASS=$((PASS+1))
else
  echo "FAIL: $call_depth_test"; FAIL=$((FAIL+1))
fi

echo "== Running src/tests/kiem_tra_stack_trace.vi [expected runtime failure] =="
stack_trace_test="src/tests/kiem_tra_stack_trace.vi"
stack_trace_out="$tmpdir/kiem_tra_stack_trace.output"
stack_trace_exp="src/tests/expected/kiem_tra_stack_trace.expected"
"$EXEC_PATH" "$stack_trace_test" > "$stack_trace_out" 2>&1
stack_trace_status=$?
if [ "$stack_trace_status" -eq 0 ]; then
  echo "FAIL: $stack_trace_test (expected non-zero exit code)"; FAIL=$((FAIL+1))
elif diff -u "$stack_trace_exp" "$stack_trace_out"; then
  echo "PASS: $stack_trace_test"; PASS=$((PASS+1))
else
  echo "FAIL: $stack_trace_test"; FAIL=$((FAIL+1))
fi

echo "== Running src/tests/kiem_tra_stack_trace_module.vi [expected runtime failure] =="
stack_trace_module_test="src/tests/kiem_tra_stack_trace_module.vi"
stack_trace_module_out="$tmpdir/kiem_tra_stack_trace_module.output"
stack_trace_module_exp="src/tests/expected/kiem_tra_stack_trace_module.expected"
"$EXEC_PATH" "$stack_trace_module_test" > "$stack_trace_module_out" 2>&1
stack_trace_module_status=$?
if [ "$stack_trace_module_status" -eq 0 ]; then
  echo "FAIL: $stack_trace_module_test (expected non-zero exit code)"; FAIL=$((FAIL+1))
elif diff -u "$stack_trace_module_exp" "$stack_trace_module_out"; then
  echo "PASS: $stack_trace_module_test"; PASS=$((PASS+1))
else
  echo "FAIL: $stack_trace_module_test"; FAIL=$((FAIL+1))
fi

# Các ca lỗi runtime dưới đây khóa cơ chế chẩn đoán từ trạng thái thực tế: VM phải
# tự suy ra nguyên nhân và giải thích mà không cần nơi phát sinh gắn mã lỗi.
for runtime_failure_test in \
  src/tests/kiem_tra_loi_chia_cho_0.vi \
  src/tests/kiem_tra_loi_chia_du_cho_0.vi \
  src/tests/kiem_tra_loi_chia_du_so_thuc.vi \
  src/tests/kiem_tra_loi_can_so_nguyen.vi \
  src/tests/kiem_tra_loi_phep_tinh_can_so.vi \
  src/tests/kiem_tra_loi_so_sanh_khac_kieu.vi \
  src/tests/kiem_tra_loi_chi_so_vuot_pham_vi.vi \
  src/tests/kiem_tra_loi_kieu_chi_so.vi \
  src/tests/kiem_tra_loi_du_lieu_khong_the_danh_chi_so.vi \
  src/tests/kiem_tra_loi_ham_khong_ton_tai.vi \
  src/tests/kiem_tra_loi_gia_tri_khong_the_goi.vi \
  src/tests/kiem_tra_loi_sai_so_luong_doi_so.vi \
  src/tests/kiem_tra_loi_khong_phai_doi_tuong.vi \
  src/tests/kiem_tra_loi_thuoc_tinh_khong_ton_tai.vi \
  src/tests/kiem_tra_loi_phuong_thuc_khong_ton_tai.vi \
  src/tests/kiem_tra_loi_truy_cap_thanh_vien_bi_cam.vi \
  src/tests/kiem_tra_loi_tang_sai_kieu.vi \
  src/tests/kiem_tra_loi_giam_sai_kieu.vi \
  src/tests/kiem_tra_loi_chuyen_so_thuc_that_bai.vi \
  src/tests/kiem_tra_loi_doc_tep_that_bai.vi; do
  runtime_failure_name="$(basename "$runtime_failure_test" .vi)"
  runtime_failure_out="$tmpdir/${runtime_failure_name}.output"
  runtime_failure_exp="src/tests/expected/${runtime_failure_name}.expected"
  echo "== Running $runtime_failure_test [expected runtime failure] =="
  "$EXEC_PATH" "$runtime_failure_test" > "$runtime_failure_out" 2>&1
  runtime_failure_status=$?
  if [ "$runtime_failure_status" -eq 0 ]; then
    echo "FAIL: $runtime_failure_test (expected non-zero exit code)"; FAIL=$((FAIL+1))
  elif diff -u "$runtime_failure_exp" "$runtime_failure_out"; then
    echo "PASS: $runtime_failure_test"; PASS=$((PASS+1))
  else
    echo "FAIL: $runtime_failure_test"; FAIL=$((FAIL+1))
  fi
done

echo "== Running src/tests/kiem_tra_jit_mvp.vi [JIT] =="
jit_out="$tmpdir/kiem_tra_jit_mvp.output"
jit_exp="src/tests/expected/kiem_tra_jit_mvp.expected"
VPP_ENABLE_JIT=1 "$EXEC_PATH" src/tests/kiem_tra_jit_mvp.vi > "$jit_out" 2>&1
jit_status=$?
if [ "$jit_status" -ne 0 ]; then
  echo "FAIL: src/tests/kiem_tra_jit_mvp.vi [JIT] (exit code: $jit_status)"; FAIL=$((FAIL+1))
  cat "$jit_out" >&2
elif diff -u "$jit_exp" "$jit_out"; then
  echo "PASS: src/tests/kiem_tra_jit_mvp.vi [JIT]"; PASS=$((PASS+1))
else
  echo "FAIL: src/tests/kiem_tra_jit_mvp.vi [JIT]"; FAIL=$((FAIL+1))
fi

echo "== Running src/tests/kiem_tra_gc_mvp.vi [GC] =="
gc_out="$tmpdir/kiem_tra_gc_mvp.output"
gc_exp="src/tests/expected/kiem_tra_gc_mvp.expected"
VPP_ENABLE_GC=1 VPP_GC_INTERVAL=1 "$EXEC_PATH" src/tests/kiem_tra_gc_mvp.vi > "$gc_out" 2>&1
gc_status=$?
if [ "$gc_status" -ne 0 ]; then
  echo "FAIL: src/tests/kiem_tra_gc_mvp.vi [GC] (exit code: $gc_status)"; FAIL=$((FAIL+1))
  cat "$gc_out" >&2
elif diff -u "$gc_exp" "$gc_out"; then
  echo "PASS: src/tests/kiem_tra_gc_mvp.vi [GC]"; PASS=$((PASS+1))
else
  echo "FAIL: src/tests/kiem_tra_gc_mvp.vi [GC]"; FAIL=$((FAIL+1))
fi

echo ""
echo "=== PASS: $PASS, FAIL: $FAIL ==="

if [ "$FAIL" -ne 0 ]; then
  exit 1
fi
