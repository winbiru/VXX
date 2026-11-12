// Chuyển đối số giữa ABI công khai và hàm thực thi V++.

// Xuất thuật toán .vi qua ABI i32 của máy đích. Đối số nằm sát nhau tại rsp trước khi gọi hàm thực
// thi.
.macro RT_EXPORT cong_khai, dich, so_doi_so
    .globl \cong_khai
\cong_khai:
#if VPP_NATIVE_RUNTIME_STACK_ABI
    subq $(40), %rsp
    .if \so_doi_so > 0
    movl %edi, (%rsp)
    .endif
    .if \so_doi_so > 1
    movl %esi, 4(%rsp)
    .endif
    .if \so_doi_so > 2
    movl %edx, 8(%rsp)
    .endif
#if defined(__APPLE__) || (defined(__linux__) && VPP_NATIVE_RUNTIME_STACK_ABI)
    call "\dich"
#else
    call \dich
#endif
    addq $(40), %rsp
    ret
#else
#ifdef __APPLE__
    jmp "\dich"
#else
    jmp \dich
#endif
#endif
.endm
RT_EXPORT PUBLIC(vpp_native_bytes_new), RT_TARGET(rt_bytes_new), 0
RT_EXPORT PUBLIC(vpp_native_bytes_length), RT_TARGET(rt_bytes_length), 1
RT_EXPORT PUBLIC(vpp_native_bytes_get), RT_TARGET(rt_bytes_get), 2
RT_EXPORT PUBLIC(vpp_native_bytes_append), RT_TARGET(rt_bytes_append), 2
RT_EXPORT PUBLIC(vpp_native_bytes_concat), RT_TARGET(rt_bytes_concat), 2
RT_EXPORT PUBLIC(vpp_native_bytes_equal), RT_TARGET(rt_bytes_equal), 2
RT_EXPORT PUBLIC(vpp_native_bytes_slice), RT_TARGET(rt_bytes_slice), 3
RT_EXPORT PUBLIC(vpp_native_bytes_find), RT_TARGET(rt_bytes_find), 2
RT_EXPORT PUBLIC(vpp_native_bytes_clear), RT_TARGET(rt_bytes_clear), 1
RT_EXPORT PUBLIC(vpp_native_bytes_print), RT_TARGET(rt_bytes_print), 1
RT_EXPORT PUBLIC(vpp_native_bytes_push_i32), RT_TARGET(rt_bytes_push_i32), 2
RT_EXPORT PUBLIC(vpp_native_bytes_load_i32), RT_TARGET(rt_bytes_load_i32), 2
RT_EXPORT PUBLIC(vpp_native_bytes_store_i32), RT_TARGET(rt_bytes_store_i32), 3
RT_EXPORT PUBLIC(vpp_native_bytes_free), RT_TARGET(rt_bytes_free), 1
RT_EXPORT PUBLIC(vpp_native_bytes_reserve_extra), RT_TARGET(rt_bytes_reserve), 2
RT_EXPORT PUBLIC(vpp_native_argv), RT_TARGET(rt_argv), 1
RT_EXPORT PUBLIC(vpp_native_file_open_read), RT_TARGET(rt_file_open_read), 1
RT_EXPORT PUBLIC(vpp_native_file_open_write), RT_TARGET(rt_file_open_write), 1
RT_EXPORT PUBLIC(vpp_native_file_read_block), RT_TARGET(rt_file_read_block), 2
RT_EXPORT PUBLIC(vpp_native_file_write_block), RT_TARGET(rt_file_write_block), 2
RT_EXPORT PUBLIC(vpp_native_file_read_byte), RT_TARGET(rt_file_read_byte), 1
RT_EXPORT PUBLIC(vpp_native_file_write_byte), RT_TARGET(rt_file_write_byte), 2
RT_EXPORT PUBLIC(vpp_native_file_close), RT_TARGET(rt_file_close), 1
RT_EXPORT PUBLIC(vpp_native_print_i32), RT_TARGET(rt_print_i32), 1
RT_EXPORT PUBLIC(vpp_native_read_i32), RT_TARGET(rt_read_i32), 0
RT_EXPORT PUBLIC(vpp_native_read_status), RT_TARGET(rt_read_status), 0
RT_EXPORT PUBLIC(vpp_native_runtime_module_path), RT_TARGET(rt_runtime_module_path), 0
RT_EXPORT PUBLIC(vpp_native_runtime_file_path), RT_TARGET(rt_runtime_file_path), 1
RT_EXPORT PUBLIC(vpp_native_object_services), RT_TARGET(rt_object_services), 3
RT_EXPORT PUBLIC(vpp_native_object_archive_entry), RT_TARGET(rt_object_archive_entry), 2
RT_EXPORT PUBLIC(vpp_native_object_archive_summary), RT_TARGET(rt_object_archive_summary), 1
RT_EXPORT PUBLIC(vpp_native_link_archive_single), RT_TARGET(rt_link_archive_single), 1
RT_EXPORT PUBLIC(vpp_native_link_validate_paths), RT_TARGET(rt_link_validate_paths), 2
RT_EXPORT PUBLIC(vpp_native_link_temporary_path), RT_TARGET(rt_link_temporary_path), 3
RT_EXPORT PUBLIC(vpp_native_link_temp_open), RT_TARGET(rt_link_temp_open), 1
RT_EXPORT PUBLIC(vpp_native_link_publish), RT_TARGET(rt_link_publish), 2
RT_EXPORT PUBLIC(vpp_native_link_spawn_wait), RT_TARGET(rt_link_spawn_wait), 0
RT_EXPORT PUBLIC(vpp_native_link_validate_files), RT_TARGET(rt_link_validate_files), 2
RT_EXPORT PUBLIC(vpp_native_link_executable_ready), RT_TARGET(rt_link_executable_ready), 1
RT_EXPORT PUBLIC(vpp_native_link_write_object), RT_TARGET(rt_link_write_object), 3
RT_EXPORT PUBLIC(vpp_native_cache_header_write), RT_TARGET(rt_cache_header_write), 2
RT_EXPORT PUBLIC(vpp_native_cache_header_check), RT_TARGET(rt_cache_header_check), 2
RT_EXPORT PUBLIC(vpp_native_cache_digest_hex), RT_TARGET(rt_cache_digest_hex), 0
RT_EXPORT PUBLIC(vpp_native_cache_read), RT_TARGET(rt_cache_read), 2
RT_EXPORT PUBLIC(vpp_native_cache_write), RT_TARGET(rt_cache_write), 2
RT_EXPORT PUBLIC(vpp_native_link_macho), RT_TARGET(rt_link_macho), 3
RT_EXPORT PUBLIC(vpp_native_link_macho_modules), RT_TARGET(rt_link_macho_modules), 3
RT_EXPORT PUBLIC(vpp_native_cache_report_entry), RT_TARGET(rt_cache_report), 0

