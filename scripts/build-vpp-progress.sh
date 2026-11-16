#!/usr/bin/env bash
# Sourced by the build scripts; preserve compiler exit codes with pipefail.
vpp_timed_command() {
  local start=$SECONDS
  (
    set -o pipefail
    { "$@" 2>&1 1>&3 | while IFS= read -r line || [[ -n "$line" ]]; do
        if [[ "$line" == '[V++ i32]'* ]]; then
          printf '[%ds] %s\n' "$((SECONDS - start))" "$line"
        else
          printf '%s\n' "$line"
        fi
      done >&2; } 3>&1
  )
}

# The full compiler emits completed phase timings and per-function records.
# Keep its normal stdout and exit status, while reporting activity on stderr.
vpp_profile_command() (
  local profile=$1
  shift
  if [[ ${VPP_NATIVE_BUILD_PROGRESS:-1} != 1 ]]; then
    "$@"
    return $?
  fi
  local profile_start=$SECONDS
  : > "$profile"
  VPP_COMPILER_PROFILE_FILE="$profile" VPP_NATIVE_PROFILE=1 "$@" &
  local compiler_pid=$!
  trap 'kill "$compiler_pid" 2>/dev/null || :; wait "$compiler_pid" 2>/dev/null || :; exit 130' INT TERM HUP
  local seen=0 last_report=$SECONDS functions_done=0
  phase_label() {
    case "$1" in
      source.graph) echo 'Nạp đồ thị mô-đun' ;;
      link.registry) echo 'Lập bảng ký hiệu' ;;
      link.types) echo 'Kiểm tra kiểu' ;;
      link.selective_imports) echo 'Giải quyết các phần nhập chọn lọc' ;;
      link.selective.detect) echo 'Phát hiện nhập chọn lọc' ;;
      link.selective.modules) echo 'Phân tích mô-đun nhập chọn lọc' ;;
      link.selective.initializers) echo 'Xác định hàm khởi tạo' ;;
      link.selective.closure) echo 'Xác định phụ thuộc bắc cầu' ;;
      link.selective.prune) echo 'Loại bỏ phần nhập không dùng' ;;
      link.edge_index) echo 'Lập chỉ mục phụ thuộc' ;;
      link.prepare_ir) echo 'Chuẩn bị biểu diễn trung gian' ;;
      link.emit_graph) echo 'Sinh biểu diễn trung gian' ;;
      link.function_table) echo 'Lập bảng hàm' ;;
      link.validate_modules) echo 'Xác thực mô-đun' ;;
      link.finalize) echo 'Hoàn thiện liên kết' ;;
      source.link) echo 'Liên kết nguồn' ;;
      artifact.to_wire) echo 'Đóng gói biểu diễn trung gian' ;;
      asm.specialize) echo 'Chuyên biệt hóa mã' ;;
      asm.fast_paths) echo 'Tối ưu đường thực thi nhanh' ;;
      asm.functions) echo 'Sinh mã cho các hàm' ;;
      asm.metadata) echo 'Sinh siêu dữ liệu mã máy' ;;
      asm.total) echo 'Hoàn tất sinh mã máy' ;;
      *) printf '%s' "$1" ;;
    esac
  }
  report_profile() {
    local line
    local phase_pattern='^VPP_COMPILER_PROFILE[[:space:]]+([^[:space:]]+)[[:space:]]+([0-9]+([.][0-9]+)?)[[:space:]]+ms$'
    while IFS= read -r line; do
      seen=$((seen + 1))
      if [[ "$line" =~ $phase_pattern ]]; then
        local prefix phase duration unit
        read -r prefix phase duration unit <<< "$line"
        printf '[V++ đầy đủ | %ds] Hoàn tất pha %s: %s ms\n' "$((SECONDS - profile_start))" "$(phase_label "$phase")" "$duration" >&2
        last_report=$SECONDS
      elif [[ "$line" == 'VPP_COMPILER_PROFILE asm.body.function id='* ]]; then
        functions_done=$((functions_done + 1))
      fi
    done < <(tail -n "+$((seen + 1))" "$profile")
  }
  while kill -0 "$compiler_pid" 2>/dev/null; do
    report_profile
    if (( SECONDS - last_report >= 30 )); then
      if (( functions_done > 0 )); then
        printf '[V++ đầy đủ | %ds] Đã ghi nhận %d thân hàm\n' "$((SECONDS - profile_start))" "$functions_done" >&2
      else
        printf '[V++ đầy đủ | %ds] Bộ biên dịch vẫn đang xử lý\n' "$((SECONDS - profile_start))" >&2
      fi
      last_report=$SECONDS
    fi
    sleep 2
  done
  report_profile
  wait "$compiler_pid"
)
