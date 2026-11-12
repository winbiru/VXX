// Chuyển đối số giữa ABI công khai và hàm thực thi V++.

// Bộ chuyển đổi ABI công khai: tầng 0 dùng AAPCS64, bộ biên dịch tự chủ dùng đối số i32 xếp sát trên
// ngăn xếp. Thuật toán nằm chung trong mã .vi.
.macro RUNTIME_EXPORT cong_khai, dich, so_doi_so
    .globl _\cong_khai
_\cong_khai:
#if VPP_NATIVE_RUNTIME_STACK_ABI
    sub sp, sp, #32
    str x30, [sp, #16]
    .if \so_doi_so > 0
    str w0, [sp]
    .endif
    .if \so_doi_so > 1
    str w1, [sp, #4]
    .endif
    .if \so_doi_so > 2
    str w2, [sp, #8]
    .endif
    bl "\dich"
    ldr x30, [sp, #16]
    add sp, sp, #32
    ret
#else
    b "\dich"
#endif
.endm
RUNTIME_EXPORT vpp_native_bytes_new, rt_bytes_new, 0
RUNTIME_EXPORT vpp_native_bytes_length, rt_bytes_length, 1
RUNTIME_EXPORT vpp_native_bytes_get, rt_bytes_get, 2
RUNTIME_EXPORT vpp_native_bytes_append, rt_bytes_append, 2
RUNTIME_EXPORT vpp_native_bytes_concat, rt_bytes_concat, 2
RUNTIME_EXPORT vpp_native_bytes_equal, rt_bytes_equal, 2
RUNTIME_EXPORT vpp_native_bytes_slice, rt_bytes_slice, 3
RUNTIME_EXPORT vpp_native_bytes_find, rt_bytes_find, 2
RUNTIME_EXPORT vpp_native_bytes_clear, rt_bytes_clear, 1
RUNTIME_EXPORT vpp_native_bytes_print, rt_bytes_print, 1
RUNTIME_EXPORT vpp_native_bytes_push_i32, rt_bytes_push_i32, 2
RUNTIME_EXPORT vpp_native_bytes_load_i32, rt_bytes_load_i32, 2
RUNTIME_EXPORT vpp_native_bytes_store_i32, rt_bytes_store_i32, 3
RUNTIME_EXPORT vpp_native_bytes_free, rt_bytes_free, 1
RUNTIME_EXPORT vpp_native_bytes_reserve_extra, rt_bytes_reserve, 2
RUNTIME_EXPORT vpp_native_argv, rt_argv, 1
RUNTIME_EXPORT vpp_native_file_open_read, rt_file_open_read, 1
RUNTIME_EXPORT vpp_native_file_open_write, rt_file_open_write, 1
RUNTIME_EXPORT vpp_native_file_read_block, rt_file_read_block, 2
RUNTIME_EXPORT vpp_native_file_write_block, rt_file_write_block, 2
RUNTIME_EXPORT vpp_native_file_read_byte, rt_file_read_byte, 1
RUNTIME_EXPORT vpp_native_file_write_byte, rt_file_write_byte, 2
RUNTIME_EXPORT vpp_native_file_close, rt_file_close, 1
RUNTIME_EXPORT vpp_native_print_i32, rt_print_i32, 1
RUNTIME_EXPORT vpp_native_read_i32, rt_read_i32, 0
RUNTIME_EXPORT vpp_native_read_status, rt_read_status, 0
RUNTIME_EXPORT vpp_native_runtime_module_path, rt_runtime_module_path, 0
RUNTIME_EXPORT vpp_native_runtime_file_path, rt_runtime_file_path, 1
RUNTIME_EXPORT vpp_native_object_services, rt_object_services, 3
RUNTIME_EXPORT vpp_native_object_archive_entry, rt_object_archive_entry, 2
RUNTIME_EXPORT vpp_native_object_archive_summary, rt_object_archive_summary, 1
RUNTIME_EXPORT vpp_native_link_archive_single, rt_link_archive_single, 1
RUNTIME_EXPORT vpp_native_link_validate_paths, rt_link_validate_paths, 2
RUNTIME_EXPORT vpp_native_link_temporary_path, rt_link_temporary_path, 3
RUNTIME_EXPORT vpp_native_link_temp_open, rt_link_temp_open, 1
RUNTIME_EXPORT vpp_native_link_publish, rt_link_publish, 2
RUNTIME_EXPORT vpp_native_link_spawn_wait, rt_link_spawn_wait, 0
RUNTIME_EXPORT vpp_native_link_validate_files, rt_link_validate_files, 2
RUNTIME_EXPORT vpp_native_link_executable_ready, rt_link_executable_ready, 1
RUNTIME_EXPORT vpp_native_link_write_object, rt_link_write_object, 3
RUNTIME_EXPORT vpp_native_cache_header_write, rt_cache_header_write, 2
RUNTIME_EXPORT vpp_native_cache_header_check, rt_cache_header_check, 2
RUNTIME_EXPORT vpp_native_cache_digest_hex, rt_cache_digest_hex, 0
RUNTIME_EXPORT vpp_native_cache_read, rt_cache_read, 2
RUNTIME_EXPORT vpp_native_cache_write, rt_cache_write, 2
RUNTIME_EXPORT vpp_native_link_macho, rt_link_macho, 3
RUNTIME_EXPORT vpp_native_link_macho_modules, rt_link_macho_modules, 3
RUNTIME_EXPORT vpp_native_cache_report_entry, rt_cache_report, 0

.zerofill __DATA,__bss,_vpp_native_arena_base,8,3
.subsections_via_symbols

