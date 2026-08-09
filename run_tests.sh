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

# HTTP tests must not depend on an external service or internet access. Start a
# tiny server implemented in V++ itself, which accepts GET/POST/PUT/DELETE and returns JSON.
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
  rm -rf "$tmpdir"
}
trap cleanup EXIT INT TERM

"$EXEC_PATH" src/tests/http_fixture.vi >"$tmpdir/http_fixture.log" 2>&1 &
HTTP_FIXTURE_PID=$!
fixture_ready=0
for _ in $(seq 1 50); do
  if curl -fsS --max-time 1 http://127.0.0.1:18080/health >"$tmpdir/http_fixture.output" 2>/dev/null &&
     [ "$(cat "$tmpdir/http_fixture.output")" = '{"ok":true}' ]; then
    fixture_ready=1
    break
  fi
  sleep 0.1
done
if [ "$fixture_ready" -ne 1 ]; then
  echo "ERROR: local HTTP test fixture did not return the expected /health response" >&2
  cat "$tmpdir/http_fixture.log" >&2
  exit 2
fi

echo "== Running examples/api_project/application.vi [example] =="
"$EXEC_PATH" examples/api_project/application.vi >"$tmpdir/api_example.log" 2>&1 &
EXAMPLE_PID=$!
example_ready=0
for _ in $(seq 1 50); do
  if curl -fsS --max-time 1 http://127.0.0.1:8080/health >"$tmpdir/api_example.output" 2>/dev/null; then
    example_ready=1
    break
  fi
  sleep 0.1
done
if [ "$example_ready" -eq 1 ] && [ "$(cat "$tmpdir/api_example.output")" = "true" ]; then
  echo "PASS: examples/api_project/application.vi [example]"; PASS=$((PASS+1))
else
  echo "FAIL: examples/api_project/application.vi [example]"; FAIL=$((FAIL+1))
  cat "$tmpdir/api_example.log" >&2
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
  for _ in $(seq 1 50); do
    if curl -fsS --max-time 1 "http://127.0.0.1:${scaffold_port}/health" >"$tmpdir/backend_scaffold.output" 2>/dev/null; then
      if [ "$(cat "$tmpdir/backend_scaffold.output")" = "true" ]; then
        scaffold_ok=1
      fi
      break
    fi
    sleep 0.1
  done
  kill "$EXAMPLE_PID" 2>/dev/null || true
  wait "$EXAMPLE_PID" 2>/dev/null || true
  EXAMPLE_PID=""
fi
if [ "$scaffold_ok" -eq 1 ]; then
  echo "PASS: backend scaffold"; PASS=$((PASS+1))
else
  echo "FAIL: backend scaffold"; FAIL=$((FAIL+1))
  cat "$tmpdir/backend_scaffold.log" >&2 2>/dev/null || true
fi

# Verify bare Vietnamese package imports from a directory outside the repository.
# This exercises the installed-layout lookup: $VPP_HOME/gói/thư viện/<tên package>/main.vi.
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
  src/tests/kiem_tra_package_modules.vi
  src/tests/kiem_tra_package_tieng_viet.vi
  src/tests/kiem_tra_stdlib.vi
  src/tests/kiem_tra_stdlib_starter.vi
  src/tests/kiem_tra_stdlib_http.vi
  src/tests/kiem_tra_stdlib_http_post_put.vi
  src/tests/kiem_tra_application_server.vi
  src/tests/kiem_tra_rest_json_jwt.vi
  src/tests/kiem_tra_stdlib_tinh_toan.vi
  src/tests/kiem_tra_stdlib_mo_rong.vi
  src/tests/kiem_tra_thu_vien_kiem_thu.vi
  src/tests/kiem_tra_json_an_toan.vi
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
  "$EXEC_PATH" "$testfile" > "$out" 2>&1
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
VPP_ENABLE_JIT=1 "$EXEC_PATH" src/tests/kiem_tra_jit_mvp.vi > "$jit_out" 2>&1
if diff -u "$jit_exp" "$jit_out"; then
  echo "PASS: src/tests/kiem_tra_jit_mvp.vi [JIT]"; PASS=$((PASS+1))
else
  echo "FAIL: src/tests/kiem_tra_jit_mvp.vi [JIT]"; FAIL=$((FAIL+1))
fi

echo "== Running src/tests/kiem_tra_gc_mvp.vi [GC] =="
gc_out="$tmpdir/kiem_tra_gc_mvp.output"
gc_exp="src/tests/expected/kiem_tra_gc_mvp.expected"
VPP_ENABLE_GC=1 VPP_GC_INTERVAL=1 "$EXEC_PATH" src/tests/kiem_tra_gc_mvp.vi > "$gc_out" 2>&1
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
