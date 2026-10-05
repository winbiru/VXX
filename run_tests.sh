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
tmpdir=$(mktemp -d "src/tests/.tmp.XXXXXX")
export VPP_PREFERENCES_HOME="$ROOT_DIR/$tmpdir/preferences"
PASS=0; FAIL=0
HTTP_NETWORK_RESTRICTED=0

# HTTP integration tests use the same TCP socket primitives as production. If
# the execution environment denies bind(2), only tests that require localhost
# sockets are skipped; protocol parsing/framing still runs as ordinary V++ tests.
HTTP_FIXTURE_PID=""
VPP_HOME_TEST_DIR=""
cleanup() {
  if [ -n "$HTTP_FIXTURE_PID" ]; then
    kill "$HTTP_FIXTURE_PID" 2>/dev/null || true
    wait "$HTTP_FIXTURE_PID" 2>/dev/null || true
  fi
  if [ -n "$VPP_HOME_TEST_DIR" ] && [ -d "$VPP_HOME_TEST_DIR" ]; then
    rm -rf "$VPP_HOME_TEST_DIR"
  fi
  rm -f src/tests/.tmp_api_project.db src/tests/.tmp_api_project.db-journal \
        src/tests/.tmp_api_project.db-shm src/tests/.tmp_api_project.db-wal
  rm -rf "$tmpdir"
}
trap cleanup EXIT INT TERM

http_get() {
  local url="$1"
  local output="$2"
  curl -fsS --max-time 1 "$url" >"$output" 2>/dev/null
}

wait_http_value() {
  local url="$1"
  local expected="$2"
  local output="$3"
  local attempts=200
  for _ in $(seq 1 "$attempts"); do
    if http_get "$url" "$output" && [ "$(cat "$output")" = "$expected" ]; then
      return 0
    fi
    if [ -n "$HTTP_FIXTURE_PID" ] && ! kill -0 "$HTTP_FIXTURE_PID" 2>/dev/null; then
      return 1
    fi
    sleep 0.1
  done
  return 1
}

"$EXEC_PATH" src/tests/http_fixture.vi >"$tmpdir/http_fixture.log" 2>&1 &
HTTP_FIXTURE_PID=$!
if ! wait_http_value "http://127.0.0.1:18080/health" '{"ok":true}' "$tmpdir/http_fixture.output"; then
  if ! kill -0 "$HTTP_FIXTURE_PID" 2>/dev/null; then
    wait "$HTTP_FIXTURE_PID" 2>/dev/null || true
  fi
  if grep -Eq 'socket_tcp_lang_nghe: bind .*( 1| 13| 10013)$' "$tmpdir/http_fixture.log"; then
    kill "$HTTP_FIXTURE_PID" 2>/dev/null || true
    wait "$HTTP_FIXTURE_PID" 2>/dev/null || true
    HTTP_FIXTURE_PID=""
    HTTP_NETWORK_RESTRICTED=1
    echo "INFO: localhost bind is restricted; skipping socket integration tests"
  else
    echo "ERROR: local HTTP test fixture did not return the expected /health response" >&2
    cat "$tmpdir/http_fixture.log" >&2
    exit 2
  fi
fi

# Verify bare Vietnamese package imports from a directory outside the repository.
# This exercises the installed-layout lookup: $VPP_HOME/gói/chuẩn/<tên package>/chính.vi.
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
  src/tests/kiem_tra_http_thuan_vpp.vi
  src/tests/kiem_tra_http_response_vpp.vi
  src/tests/kiem_tra_http_may_chu_thuan_vpp.vi
  src/tests/kiem_tra_http_transport_bat_loi.vi
  src/tests/kiem_tra_application_server.vi
  src/tests/kiem_tra_rest_json_jwt.vi
  src/tests/kiem_tra_stdlib_tinh_toan.vi
  src/tests/kiem_tra_stdlib_mo_rong.vi
  src/tests/kiem_tra_stdlib_crypto.vi
  src/tests/kiem_tra_goi_kiem_thu.vi
  src/tests/kiem_tra_goi_toan_hoc.vi
  src/tests/kiem_tra_goi_vat_ly.vi
  src/tests/kiem_tra_goi_hoa_hoc.vi
  src/tests/kiem_tra_goi_bo_suu_tap.vi
  src/tests/kiem_tra_goi_van_ban.vi
  src/tests/kiem_tra_goi_thoi_gian.vi
  src/tests/kiem_tra_goi_thoi_gian_1_0.vi
  src/tests/kiem_tra_goi_json.vi
  src/tests/kiem_tra_goi_nhat_ky_moi.vi
  src/tests/kiem_tra_goi_nhat_ky_1_0.vi
  src/tests/kiem_tra_goi_http.vi
  src/tests/kiem_tra_goi_mat_ma.vi
  src/tests/kiem_tra_goi_cau_hinh.vi
  src/tests/kiem_tra_goi_regex.vi
  src/tests/kiem_tra_goi_xml.vi
  src/tests/kiem_tra_goi_luu_tru.vi
  src/tests/kiem_tra_goi_dong_thoi.vi
  src/tests/kiem_tra_goi_tuy_chon.vi
  src/tests/kiem_tra_goi_bang_ma.vi
  src/tests/kiem_tra_goi_ban_dia_hoa.vi
  src/tests/kiem_tra_goi_ngau_nhien.vi
  src/tests/kiem_tra_goi_dns.vi
  src/tests/kiem_tra_goi_dau_noi_mang.vi
  src/tests/kiem_tra_goi_tap_ban_ghi.vi
  src/tests/kiem_tra_goi_xml_dom.vi
  src/tests/kiem_tra_goi_zipfs.vi
  src/tests/kiem_tra_goi_may_chu_http.vi
  src/tests/kiem_tra_goi_bien_dich.vi
  src/tests/kiem_tra_goi_phan_tich_phu_thuoc.vi
  src/tests/kiem_tra_goi_tai_lieu.vi
  src/tests/kiem_tra_goi_kich_ban.vi
  src/tests/kiem_tra_goi_repl.vi
  src/tests/kiem_tra_goi_soan_thao_tuong_tac.vi
  src/tests/kiem_tra_goi_cong_cu_luu_tru.vi
  src/tests/kiem_tra_goi_dong_goi_ung_dung.vi
  src/tests/kiem_tra_goi_mang.vi
  src/tests/kiem_tra_stdlib_nen_tang.vi
  src/tests/kiem_tra_chuoi_nen_tang.vi
  src/tests/kiem_tra_goi_ham.vi
  src/tests/kiem_tra_tuy_chon_thong_ke.vi
  src/tests/kiem_tra_gia_tri.vi
  src/tests/kiem_tra_dinh_dang.vi
  src/tests/kiem_tra_bo_noi_chuoi.vi
  src/tests/kiem_tra_luong_du_lieu.vi
  src/tests/kiem_tra_codec_uuid_bitset.vi
  src/tests/kiem_tra_quet_tach_chuoi.vi
  src/tests/kiem_tra_su_kien.vi
  src/tests/kiem_tra_json_phan_tich.vi
  src/tests/kiem_tra_json_an_toan.vi
  src/tests/kiem_tra_json_file_roundtrip.vi
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

RUNNABLE_TESTS=()
for testfile in "${TESTS[@]}"; do
  base=$(basename "${testfile%.vi}")
  if [ "$HTTP_NETWORK_RESTRICTED" -eq 1 ]; then
    case "$base" in
      kiem_tra_stdlib_http|kiem_tra_stdlib_http_post_put|kiem_tra_goi_http|kiem_tra_goi_dau_noi_mang)
        continue
        ;;
    esac
  fi
  RUNNABLE_TESTS+=("$testfile")
done

# Chạy các golden test thường trong một tiến trình V++ duy nhất. Worker vẫn ghi
# output/status riêng cho từng file nên phần so sánh expected bên dưới giữ nguyên
# semantics, trong khi compiler có thể tái sử dụng AST module giữa các test.
if ! "$EXEC_PATH" --batch-run-tests "$tmpdir" "${RUNNABLE_TESTS[@]}"; then
  echo "ERROR: batch test worker failed" >&2
  exit 2
fi

for testfile in "${TESTS[@]}"; do
  base=$(basename "${testfile%.vi}")
  if [ "$HTTP_NETWORK_RESTRICTED" -eq 1 ]; then
    case "$base" in
      kiem_tra_stdlib_http|kiem_tra_stdlib_http_post_put|kiem_tra_goi_http|kiem_tra_goi_dau_noi_mang)
        echo "== Skipping $testfile: localhost socket syscalls are restricted =="
        continue
        ;;
    esac
  fi
  out="$tmpdir/$base.output"
  exp="src/tests/expected/$base.expected"
  echo "== Running $testfile =="
  if [ -f "$tmpdir/$base.status" ]; then
    test_status=$(cat "$tmpdir/$base.status")
  else
    echo "FAIL: $testfile (batch worker did not produce status)"; FAIL=$((FAIL+1))
    continue
  fi
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

# Các ca lỗi runtime dưới đây khóa cơ chế chẩn đoán từ trạng thái thực tế: VM phải
# tự suy ra nguyên nhân và giải thích mà không cần nơi phát sinh gắn mã lỗi.
EXPECTED_FAILURE_TESTS=( \
  src/tests/kiem_tra_de_quy_vuot_gioi_han.vi \
  src/tests/kiem_tra_stack_trace.vi \
  src/tests/kiem_tra_stack_trace_module.vi \
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
  src/tests/kiem_tra_loi_doc_tep_that_bai.vi
)

# Các ca expected-failure cũng chạy trong một worker duy nhất để tái sử dụng
# AST/mid-end cache. Mỗi test vẫn tạo CompilationContext và VM riêng.
if ! "$EXEC_PATH" --batch-run-tests "$tmpdir" "${EXPECTED_FAILURE_TESTS[@]}"; then
  echo "ERROR: expected-failure batch worker failed" >&2
  exit 2
fi

for runtime_failure_test in "${EXPECTED_FAILURE_TESTS[@]}"; do
  runtime_failure_name="$(basename "$runtime_failure_test" .vi)"
  runtime_failure_out="$tmpdir/${runtime_failure_name}.output"
  runtime_failure_exp="src/tests/expected/${runtime_failure_name}.expected"
  echo "== Running $runtime_failure_test [expected runtime failure] =="
  if [ -f "$tmpdir/${runtime_failure_name}.status" ]; then
    runtime_failure_status=$(cat "$tmpdir/${runtime_failure_name}.status")
  else
    echo "FAIL: $runtime_failure_test (batch worker did not produce status)"; FAIL=$((FAIL+1))
    continue
  fi
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
