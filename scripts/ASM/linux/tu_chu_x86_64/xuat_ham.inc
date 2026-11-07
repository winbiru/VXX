// Chuyển đối số giữa ABI công khai và hàm thực thi V++.

.globl vpp_native_bytes_new
vpp_native_bytes_new:
    subq $(40), %rsp
    call "vpp_fn_thực thi các byte mới"
    addq $(40), %rsp
    ret
.globl vpp_native_bytes_length
vpp_native_bytes_length:
    subq $(40), %rsp
    movl %edi, (%rsp)
    call "vpp_fn_thực thi các byte chiều dài"
    addq $(40), %rsp
    ret
.globl vpp_native_bytes_get
vpp_native_bytes_get:
    subq $(40), %rsp
    movl %edi, (%rsp)
    movl %esi, 4(%rsp)
    call "vpp_fn_thực thi các byte lấy"
    addq $(40), %rsp
    ret
.globl vpp_native_bytes_append
vpp_native_bytes_append:
    subq $(40), %rsp
    movl %edi, (%rsp)
    movl %esi, 4(%rsp)
    call "vpp_fn_thực thi các byte thêm cuối"
    addq $(40), %rsp
    ret
.globl vpp_native_bytes_concat
vpp_native_bytes_concat:
    subq $(40), %rsp
    movl %edi, (%rsp)
    movl %esi, 4(%rsp)
    call "vpp_fn_thực thi các byte nối"
    addq $(40), %rsp
    ret
.globl vpp_native_bytes_equal
vpp_native_bytes_equal:
    subq $(40), %rsp
    movl %edi, (%rsp)
    movl %esi, 4(%rsp)
    call "vpp_fn_thực thi các byte bằng nhau"
    addq $(40), %rsp
    ret
.globl vpp_native_bytes_slice
vpp_native_bytes_slice:
    subq $(40), %rsp
    movl %edi, (%rsp)
    movl %esi, 4(%rsp)
    movl %edx, 8(%rsp)
    call "vpp_fn_thực thi các byte cắt"
    addq $(40), %rsp
    ret
.globl vpp_native_bytes_find
vpp_native_bytes_find:
    subq $(40), %rsp
    movl %edi, (%rsp)
    movl %esi, 4(%rsp)
    call "vpp_fn_thực thi các byte tìm"
    addq $(40), %rsp
    ret
.globl vpp_native_bytes_clear
vpp_native_bytes_clear:
    subq $(40), %rsp
    movl %edi, (%rsp)
    call "vpp_fn_thực thi các byte làm rỗng"
    addq $(40), %rsp
    ret
.globl vpp_native_bytes_print
vpp_native_bytes_print:
    subq $(40), %rsp
    movl %edi, (%rsp)
    call "vpp_fn_thực thi các byte in"
    addq $(40), %rsp
    ret
.globl vpp_native_bytes_push_i32
vpp_native_bytes_push_i32:
    subq $(40), %rsp
    movl %edi, (%rsp)
    movl %esi, 4(%rsp)
    call "vpp_fn_thực thi các byte đẩy i32"
    addq $(40), %rsp
    ret
.globl vpp_native_bytes_load_i32
vpp_native_bytes_load_i32:
    subq $(40), %rsp
    movl %edi, (%rsp)
    movl %esi, 4(%rsp)
    call "vpp_fn_thực thi các byte nạp i32"
    addq $(40), %rsp
    ret
.globl vpp_native_bytes_store_i32
vpp_native_bytes_store_i32:
    subq $(40), %rsp
    movl %edi, (%rsp)
    movl %esi, 4(%rsp)
    movl %edx, 8(%rsp)
    call "vpp_fn_thực thi các byte lưu i32"
    addq $(40), %rsp
    ret
.globl vpp_native_bytes_free
vpp_native_bytes_free:
    subq $(40), %rsp
    movl %edi, (%rsp)
    call "vpp_fn_thực thi các byte giải phóng"
    addq $(40), %rsp
    ret
.globl vpp_native_bytes_reserve_extra
vpp_native_bytes_reserve_extra:
    subq $(40), %rsp
    movl %edi, (%rsp)
    movl %esi, 4(%rsp)
    call "vpp_fn_thực thi các byte dự trữ"
    addq $(40), %rsp
    ret
.globl vpp_native_argv
vpp_native_argv:
    subq $(40), %rsp
    movl %edi, (%rsp)
    call "vpp_fn_thực thi đối số dòng lệnh"
    addq $(40), %rsp
    ret
.globl vpp_native_file_open_read
vpp_native_file_open_read:
    subq $(40), %rsp
    movl %edi, (%rsp)
    call "vpp_fn_thực thi tệp dữ liệu mở đọc"
    addq $(40), %rsp
    ret
.globl vpp_native_file_open_write
vpp_native_file_open_write:
    subq $(40), %rsp
    movl %edi, (%rsp)
    call "vpp_fn_thực thi tệp dữ liệu mở ghi"
    addq $(40), %rsp
    ret
.globl vpp_native_file_read_block
vpp_native_file_read_block:
    subq $(40), %rsp
    movl %edi, (%rsp)
    movl %esi, 4(%rsp)
    call "vpp_fn_thực thi tệp dữ liệu đọc khối"
    addq $(40), %rsp
    ret
.globl vpp_native_file_write_block
vpp_native_file_write_block:
    subq $(40), %rsp
    movl %edi, (%rsp)
    movl %esi, 4(%rsp)
    call "vpp_fn_thực thi tệp dữ liệu ghi khối"
    addq $(40), %rsp
    ret
.globl vpp_native_file_read_byte
vpp_native_file_read_byte:
    subq $(40), %rsp
    movl %edi, (%rsp)
    call "vpp_fn_thực thi tệp dữ liệu đọc byte"
    addq $(40), %rsp
    ret
.globl vpp_native_file_write_byte
vpp_native_file_write_byte:
    subq $(40), %rsp
    movl %edi, (%rsp)
    movl %esi, 4(%rsp)
    call "vpp_fn_thực thi tệp dữ liệu ghi byte"
    addq $(40), %rsp
    ret
.globl vpp_native_file_close
vpp_native_file_close:
    subq $(40), %rsp
    movl %edi, (%rsp)
    call "vpp_fn_thực thi tệp dữ liệu đóng"
    addq $(40), %rsp
    ret
.globl vpp_native_print_i32
vpp_native_print_i32:
    subq $(40), %rsp
    movl %edi, (%rsp)
    call "vpp_fn_thực thi in i32"
    addq $(40), %rsp
    ret
.globl vpp_native_read_i32
vpp_native_read_i32:
    subq $(40), %rsp
    call "vpp_fn_thực thi đọc i32"
    addq $(40), %rsp
    ret
.globl vpp_native_read_status
vpp_native_read_status:
    subq $(40), %rsp
    call "vpp_fn_thực thi đọc mã trạng thái"
    addq $(40), %rsp
    ret
.globl vpp_native_runtime_module_path
vpp_native_runtime_module_path:
    subq $(40), %rsp
    call "vpp_fn_thực thi môi trường thực thi mô đun đường tệp"
    addq $(40), %rsp
    ret
.globl vpp_native_runtime_file_path
vpp_native_runtime_file_path:
    subq $(40), %rsp
    movl %edi, (%rsp)
    call "vpp_fn_thực thi môi trường thực thi tệp dữ liệu đường tệp"
    addq $(40), %rsp
    ret
.globl vpp_native_object_services
vpp_native_object_services:
    subq $(40), %rsp
    movl %edi, (%rsp)
    movl %esi, 4(%rsp)
    movl %edx, 8(%rsp)
    call "vpp_fn_thực thi đối tượng các dịch vụ"
    addq $(40), %rsp
    ret
.globl vpp_native_object_archive_entry
vpp_native_object_archive_entry:
    subq $(40), %rsp
    movl %edi, (%rsp)
    movl %esi, 4(%rsp)
    call "vpp_fn_thực thi đối tượng kho lưu trữ điểm vào"
    addq $(40), %rsp
    ret
.globl vpp_native_object_archive_summary
vpp_native_object_archive_summary:
    subq $(40), %rsp
    movl %edi, (%rsp)
    call "vpp_fn_thực thi đối tượng kho lưu trữ bản tổng hợp"
    addq $(40), %rsp
    ret
.globl vpp_native_link_archive_single
vpp_native_link_archive_single:
    subq $(40), %rsp
    movl %edi, (%rsp)
    call "vpp_fn_thực thi liên kết kho lưu trữ đơn"
    addq $(40), %rsp
    ret
.globl vpp_native_link_validate_paths
vpp_native_link_validate_paths:
    subq $(40), %rsp
    movl %edi, (%rsp)
    movl %esi, 4(%rsp)
    call "vpp_fn_thực thi liên kết kiểm tra hợp lệ paths"
    addq $(40), %rsp
    ret
.globl vpp_native_link_temporary_path
vpp_native_link_temporary_path:
    subq $(40), %rsp
    movl %edi, (%rsp)
    movl %esi, 4(%rsp)
    movl %edx, 8(%rsp)
    call "vpp_fn_thực thi liên kết tạm thời đường tệp"
    addq $(40), %rsp
    ret
.globl vpp_native_link_temp_open
vpp_native_link_temp_open:
    subq $(40), %rsp
    movl %edi, (%rsp)
    call "vpp_fn_thực thi liên kết tạm mở"
    addq $(40), %rsp
    ret
.globl vpp_native_link_publish
vpp_native_link_publish:
    subq $(40), %rsp
    movl %edi, (%rsp)
    movl %esi, 4(%rsp)
    call "vpp_fn_thực thi liên kết công bố"
    addq $(40), %rsp
    ret
.globl vpp_native_link_spawn_wait
vpp_native_link_spawn_wait:
    subq $(40), %rsp
    call "vpp_fn_thực thi liên kết khởi chạy đợi"
    addq $(40), %rsp
    ret
.globl vpp_native_link_validate_files
vpp_native_link_validate_files:
    subq $(40), %rsp
    movl %edi, (%rsp)
    movl %esi, 4(%rsp)
    call "vpp_fn_thực thi liên kết kiểm tra hợp lệ files"
    addq $(40), %rsp
    ret
.globl vpp_native_link_executable_ready
vpp_native_link_executable_ready:
    subq $(40), %rsp
    movl %edi, (%rsp)
    call "vpp_fn_thực thi liên kết executable sẵn sàng"
    addq $(40), %rsp
    ret
.globl vpp_native_link_write_object
vpp_native_link_write_object:
    subq $(40), %rsp
    movl %edi, (%rsp)
    movl %esi, 4(%rsp)
    movl %edx, 8(%rsp)
    call "vpp_fn_thực thi liên kết ghi đối tượng"
    addq $(40), %rsp
    ret
.globl vpp_native_cache_header_write
vpp_native_cache_header_write:
    subq $(40), %rsp
    movl %edi, (%rsp)
    movl %esi, 4(%rsp)
    call "vpp_fn_thực thi bộ nhớ đệm phần đầu ghi"
    addq $(40), %rsp
    ret
.globl vpp_native_cache_header_check
vpp_native_cache_header_check:
    subq $(40), %rsp
    movl %edi, (%rsp)
    movl %esi, 4(%rsp)
    call "vpp_fn_thực thi bộ nhớ đệm phần đầu kiểm tra"
    addq $(40), %rsp
    ret
.globl vpp_native_cache_digest_hex
vpp_native_cache_digest_hex:
    subq $(40), %rsp
    call "vpp_fn_thực thi bộ nhớ đệm băm thập lục phân"
    addq $(40), %rsp
    ret
.globl vpp_native_cache_read
vpp_native_cache_read:
    subq $(40), %rsp
    movl %edi, (%rsp)
    movl %esi, 4(%rsp)
    call "vpp_fn_thực thi bộ nhớ đệm đọc"
    addq $(40), %rsp
    ret
.globl vpp_native_cache_write
vpp_native_cache_write:
    subq $(40), %rsp
    movl %edi, (%rsp)
    movl %esi, 4(%rsp)
    call "vpp_fn_thực thi bộ nhớ đệm ghi"
    addq $(40), %rsp
    ret
.globl vpp_native_link_macho
vpp_native_link_macho:
    subq $(40), %rsp
    movl %edi, (%rsp)
    movl %esi, 4(%rsp)
    movl %edx, 8(%rsp)
    call "vpp_fn_thực thi liên kết macho"
    addq $(40), %rsp
    ret
.globl vpp_native_link_macho_modules
vpp_native_link_macho_modules:
    subq $(40), %rsp
    movl %edi, (%rsp)
    movl %esi, 4(%rsp)
    movl %edx, 8(%rsp)
    call "vpp_fn_thực thi liên kết macho các mô đun"
    addq $(40), %rsp
    ret
.globl vpp_native_cache_report_entry
vpp_native_cache_report_entry:
    subq $(40), %rsp
    call "vpp_fn_thực thi bộ nhớ đệm báo cáo"
    addq $(40), %rsp
    ret
