#!/usr/bin/env bash
set -u

if [ -x "bin/vpp-cli" ]; then
  EXEC="bin/vpp-cli"
elif [ -x "cmake-build-debug/bin/vpp-cli" ]; then
  EXEC="cmake-build-debug/bin/vpp-cli"
else
  EXEC="cmake-build-debug/bin/vietvm-cli"
fi
tmpdir="src/tests/.tmp"
mkdir -p "$tmpdir"
PASS=0; FAIL=0

# HTTP tests must not depend on an external service or internet access. Start a
# tiny server implemented in V++ itself, which accepts GET/POST/PUT and returns JSON.
HTTP_FIXTURE_PID=""
cleanup() {
  if [ -n "$HTTP_FIXTURE_PID" ]; then
    kill "$HTTP_FIXTURE_PID" 2>/dev/null || true
    wait "$HTTP_FIXTURE_PID" 2>/dev/null || true
  fi
  rm -rf "$tmpdir"
}
trap cleanup EXIT INT TERM

"$EXEC" src/tests/http_fixture.vi >"$tmpdir/http_fixture.log" 2>&1 &
HTTP_FIXTURE_PID=$!
for _ in $(seq 1 50); do
  if curl -fsS --max-time 1 http://127.0.0.1:18080/health >/dev/null 2>&1; then
    break
  fi
  sleep 0.1
done
if ! curl -fsS --max-time 1 http://127.0.0.1:18080/health >/dev/null 2>&1; then
  echo "ERROR: local HTTP test fixture did not start" >&2
  exit 2
fi

TESTS=(
  src/tests/import_main.vi
  src/tests/kiem_tra_boolean.vi
  src/tests/kiem_tra_bo_qua.vi
  src/tests/kiem_tra_chon_ca.vi
  src/tests/kiem_tra_de_quy.vi
  src/tests/kiem_tra_ngoai_le.vi
  src/tests/kiem_tra_dieu_kien_long_nhieu_cap.vi
  src/tests/kiem_tra_dieu_kien_phu_dinh.vi
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
  src/tests/kiem_tra_stdlib.vi
  src/tests/kiem_tra_stdlib_starter.vi
  src/tests/kiem_tra_stdlib_http.vi
  src/tests/kiem_tra_stdlib_http_post_put.vi
  src/tests/kiem_tra_application_server.vi
  src/tests/kiem_tra_rest_json_jwt.vi
  src/tests/kiem_tra_stdlib_tinh_toan.vi
  src/tests/kiem_tra_stdlib_mo_rong.vi
  src/tests/kiem_tra_stdlib_io_config_time.vi
  src/tests/kiem_tra_api_thuc_thu.vi
  src/tests/kiem_tra_api_db_project.vi
  src/tests/kiem_tra_thu_vien_spring.vi
  src/tests/kiem_tra_thu_vien_lop.vi
  src/tests/kiem_tra_lambda_hof_mac_dinh.vi
  src/tests/kiem_tra_toan_tu_moi.vi
  src/tests/kiem_tra_lop_truy_cap.vi
  src/tests/kiem_tra_cu_phap_modifier_cu.vi
  src/tests/kiem_tra_tra_ve.vi
  src/tests/program.vi
)

for testfile in "${TESTS[@]}"; do
  base=$(basename "${testfile%.vi}")
  out="$tmpdir/$base.output"
  exp="src/tests/expected/$base.expected"
  echo "== Running $testfile =="
  "$EXEC" "$testfile" > "$out" 2>&1
  if [ -f "$exp" ]; then
    if diff -u "$exp" "$out"; then
      echo "PASS: $testfile"; PASS=$((PASS+1))
    else
      echo "FAIL: $testfile"; FAIL=$((FAIL+1))
    fi
  else
    echo "NO EXPECTED: $testfile"; FAIL=$((FAIL+1))
  fi
done

echo "== Running src/tests/kiem_tra_jit_mvp.vi [JIT] =="
jit_out="$tmpdir/kiem_tra_jit_mvp.output"
jit_exp="src/tests/expected/kiem_tra_jit_mvp.expected"
VPP_ENABLE_JIT=1 "$EXEC" src/tests/kiem_tra_jit_mvp.vi > "$jit_out" 2>&1
if diff -u "$jit_exp" "$jit_out"; then
  echo "PASS: src/tests/kiem_tra_jit_mvp.vi [JIT]"; PASS=$((PASS+1))
else
  echo "FAIL: src/tests/kiem_tra_jit_mvp.vi [JIT]"; FAIL=$((FAIL+1))
fi

echo "== Running src/tests/kiem_tra_gc_mvp.vi [GC] =="
gc_out="$tmpdir/kiem_tra_gc_mvp.output"
gc_exp="src/tests/expected/kiem_tra_gc_mvp.expected"
VPP_ENABLE_GC=1 VPP_GC_INTERVAL=1 "$EXEC" src/tests/kiem_tra_gc_mvp.vi > "$gc_out" 2>&1
if diff -u "$gc_exp" "$gc_out"; then
  echo "PASS: src/tests/kiem_tra_gc_mvp.vi [GC]"; PASS=$((PASS+1))
else
  echo "FAIL: src/tests/kiem_tra_gc_mvp.vi [GC]"; FAIL=$((FAIL+1))
fi

echo ""
echo "=== PASS: $PASS, FAIL: $FAIL ==="

if [ "$FAIL" -ne 0 ]; then
  exit 1
fi
