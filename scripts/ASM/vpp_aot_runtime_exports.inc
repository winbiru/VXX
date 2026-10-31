// Full native value ABI extends the existing platform boundary.
#if defined(VPP_NATIVE_DYNAMIC_RUNTIME) && VPP_NATIVE_DYNAMIC_RUNTIME
DYNAMIC_EXPORT AOT_PUBLIC(vpp_native_dynamic_integer), AOT_PUBLIC(vpp_aot_integer)
DYNAMIC_EXPORT AOT_PUBLIC(vpp_native_dynamic_string), AOT_PUBLIC(vpp_aot_string)
DYNAMIC_EXPORT AOT_PUBLIC(vpp_native_dynamic_floating), AOT_PUBLIC(vpp_aot_floating)
DYNAMIC_EXPORT AOT_PUBLIC(vpp_native_dynamic_null), AOT_PUBLIC(vpp_aot_null)
DYNAMIC_EXPORT AOT_PUBLIC(vpp_native_dynamic_load), AOT_PUBLIC(vpp_aot_load)
DYNAMIC_EXPORT AOT_PUBLIC(vpp_native_dynamic_initialize), AOT_PUBLIC(vpp_aot_initialize)
DYNAMIC_EXPORT AOT_PUBLIC(vpp_native_dynamic_assign), AOT_PUBLIC(vpp_aot_assign)
DYNAMIC_EXPORT AOT_PUBLIC(vpp_native_dynamic_assign_slot), AOT_PUBLIC(vpp_aot_assign_slot)
DYNAMIC_EXPORT AOT_PUBLIC(vpp_native_dynamic_store_integer), AOT_PUBLIC(vpp_aot_store_integer)
DYNAMIC_EXPORT AOT_PUBLIC(vpp_native_dynamic_parameter), AOT_PUBLIC(vpp_aot_parameter)
DYNAMIC_EXPORT AOT_PUBLIC(vpp_native_dynamic_increment), AOT_PUBLIC(vpp_aot_increment)
DYNAMIC_EXPORT AOT_PUBLIC(vpp_native_dynamic_increment_slot), AOT_PUBLIC(vpp_aot_increment_slot)
DYNAMIC_EXPORT AOT_PUBLIC(vpp_native_dynamic_update_integer_slot), AOT_PUBLIC(vpp_aot_update_integer_slot)
DYNAMIC_EXPORT AOT_PUBLIC(vpp_native_dynamic_update_numeric_slots), AOT_PUBLIC(vpp_aot_update_numeric_slots)
DYNAMIC_EXPORT AOT_PUBLIC(vpp_native_dynamic_compare_integer_slot), AOT_PUBLIC(vpp_aot_compare_integer_slot)
DYNAMIC_EXPORT AOT_PUBLIC(vpp_native_dynamic_compare_numeric_slots), AOT_PUBLIC(vpp_aot_compare_numeric_slots)
DYNAMIC_EXPORT AOT_PUBLIC(vpp_native_dynamic_counted_scalar_loop), AOT_PUBLIC(vpp_aot_counted_scalar_loop)
DYNAMIC_EXPORT AOT_PUBLIC(vpp_native_dynamic_simple_scalar_loop), AOT_PUBLIC(vpp_aot_simple_scalar_loop)
DYNAMIC_EXPORT AOT_PUBLIC(vpp_native_dynamic_numeric_add_loop), AOT_PUBLIC(vpp_aot_numeric_add_loop)
DYNAMIC_EXPORT AOT_PUBLIC(vpp_native_dynamic_binary), AOT_PUBLIC(vpp_aot_binary)
DYNAMIC_EXPORT AOT_PUBLIC(vpp_native_dynamic_negate), AOT_PUBLIC(vpp_aot_negate)
DYNAMIC_EXPORT AOT_PUBLIC(vpp_native_dynamic_list), AOT_PUBLIC(vpp_aot_list)
DYNAMIC_EXPORT AOT_PUBLIC(vpp_native_dynamic_map), AOT_PUBLIC(vpp_aot_map)
DYNAMIC_EXPORT AOT_PUBLIC(vpp_native_dynamic_index), AOT_PUBLIC(vpp_aot_index)
DYNAMIC_EXPORT AOT_PUBLIC(vpp_native_dynamic_set_index), AOT_PUBLIC(vpp_aot_set_index)
DYNAMIC_EXPORT AOT_PUBLIC(vpp_native_dynamic_call), AOT_PUBLIC(vpp_aot_call)
DYNAMIC_EXPORT AOT_PUBLIC(vpp_native_dynamic_indirect), AOT_PUBLIC(vpp_aot_indirect)
DYNAMIC_EXPORT AOT_PUBLIC(vpp_native_dynamic_closure), AOT_PUBLIC(vpp_aot_closure)
DYNAMIC_EXPORT AOT_PUBLIC(vpp_native_dynamic_define_class), AOT_PUBLIC(vpp_aot_define_class)
DYNAMIC_EXPORT AOT_PUBLIC(vpp_native_dynamic_define_method), AOT_PUBLIC(vpp_aot_define_method)
DYNAMIC_EXPORT AOT_PUBLIC(vpp_native_dynamic_construct), AOT_PUBLIC(vpp_aot_construct)
DYNAMIC_EXPORT AOT_PUBLIC(vpp_native_dynamic_property), AOT_PUBLIC(vpp_aot_property)
DYNAMIC_EXPORT AOT_PUBLIC(vpp_native_dynamic_method), AOT_PUBLIC(vpp_aot_method)
DYNAMIC_EXPORT AOT_PUBLIC(vpp_native_dynamic_print), AOT_PUBLIC(vpp_aot_print)
DYNAMIC_EXPORT AOT_PUBLIC(vpp_native_dynamic_return_value), AOT_PUBLIC(vpp_aot_return_value)
DYNAMIC_EXPORT AOT_PUBLIC(vpp_native_dynamic_return_slot), AOT_PUBLIC(vpp_aot_return_slot)
DYNAMIC_EXPORT AOT_PUBLIC(vpp_native_dynamic_return_integer), AOT_PUBLIC(vpp_aot_return_integer)
DYNAMIC_EXPORT AOT_PUBLIC(vpp_native_dynamic_intrinsic), AOT_PUBLIC(vpp_aot_intrinsic)
DYNAMIC_EXPORT AOT_PUBLIC(vpp_native_dynamic_foreign), AOT_PUBLIC(vpp_aot_foreign)
DYNAMIC_EXPORT AOT_PUBLIC(vpp_native_dynamic_switch_begin), AOT_PUBLIC(vpp_aot_switch_begin)
DYNAMIC_EXPORT AOT_PUBLIC(vpp_native_dynamic_switch_end), AOT_PUBLIC(vpp_aot_switch_end)
DYNAMIC_EXPORT AOT_PUBLIC(vpp_native_dynamic_throw_value), AOT_PUBLIC(vpp_aot_throw_value)
DYNAMIC_EXPORT AOT_PUBLIC(vpp_native_dynamic_try_begin), AOT_PUBLIC(vpp_aot_try_begin)
DYNAMIC_EXPORT AOT_PUBLIC(vpp_native_dynamic_try_end), AOT_PUBLIC(vpp_aot_try_end)
DYNAMIC_EXPORT AOT_PUBLIC(vpp_native_dynamic_catch_value), AOT_PUBLIC(vpp_aot_catch_value)
DYNAMIC_EXPORT AOT_PUBLIC(vpp_native_dynamic_safepoint), AOT_PUBLIC(vpp_aot_safepoint)
DYNAMIC_EXPORT AOT_PUBLIC(vpp_native_dynamic_condition), AOT_PUBLIC(vpp_aot_condition)
DYNAMIC_EXPORT AOT_PUBLIC(vpp_native_dynamic_case), AOT_PUBLIC(vpp_aot_case)
DYNAMIC_EXPORT AOT_PUBLIC(vpp_native_dynamic_default), AOT_PUBLIC(vpp_aot_default)
DYNAMIC_EXPORT AOT_PUBLIC(vpp_native_dynamic_failed), AOT_PUBLIC(vpp_aot_failed)
DYNAMIC_EXPORT AOT_PUBLIC(vpp_native_dynamic_handle_error), AOT_PUBLIC(vpp_aot_handle_error)
#else
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
#endif
