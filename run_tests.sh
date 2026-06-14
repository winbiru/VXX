#!/bin/zsh
EXEC="cmake-build-debug/bin/vietvm-cli"
tmpdir="src/tests/.tmp"
mkdir -p "$tmpdir"
PASS=0; FAIL=0

TESTS=(
  src/tests/import_main.vi
  src/tests/kiem_tra_boolean.vi
  src/tests/kiem_tra_bo_qua.vi
  src/tests/kiem_tra_chon_ca.vi
  src/tests/kiem_tra_de_quy.vi
  src/tests/kiem_tra_dieu_kien_long_nhieu_cap.vi
  src/tests/kiem_tra_dieu_kien_phu_dinh.vi
  src/tests/kiem_tra_ham_4_tham_so.vi
  src/tests/kiem_tra_ham_tham_so.vi
  src/tests/kiem_tra_mang_3_chieu.vi
  src/tests/kiem_tra_noi_chuoi.vi
  src/tests/kiem_tra_so_chan_1-20.vi
  src/tests/kiem_tra_so_chia_het_cho_3_va_4.vi
  src/tests/kiem_tra_so_le_chia_het_cho_5.vi
  src/tests/kiem_tra_so_nguyen.vi
  src/tests/kiem_tra_toan_tu_moi.vi
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

rm -rf "$tmpdir"
echo ""
echo "=== PASS: $PASS, FAIL: $FAIL ==="

