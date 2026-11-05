#pragma once

#include <array>
#include <cstddef>

namespace vietvm::native {

struct NativeCoreAbiEntry {
    int id;
    int arity;
    const char *symbol;
};

// ABI nguồn lõi i32. ID 39 là print_i32 nội bộ; 62..77 và 79..81 là
// primitive mức thấp nên không được phát từ lời gọi tên của mã nguồn.
inline constexpr std::array<NativeCoreAbiEntry, 62> kNativeCoreAbi = {{
    {0, 0, "vpp_native_bytes_new"},
    {1, 1, "vpp_native_bytes_length"},
    {2, 2, "vpp_native_bytes_get"},
    {3, 2, "vpp_native_bytes_append"},
    {4, 2, "vpp_native_bytes_push_i32"},
    {5, 2, "vpp_native_bytes_load_i32"},
    {6, 3, "vpp_native_bytes_store_i32"},
    {7, 1, "vpp_native_bytes_clear"},
    {8, 2, "vpp_native_bytes_concat"},
    {9, 2, "vpp_native_bytes_equal"},
    {10, 1, "vpp_native_bytes_free"},
    {11, 0, "vpp_native_argc"},
    {12, 1, "vpp_native_argv"},
    {13, 1, "vpp_native_file_open_read"},
    {14, 2, "vpp_native_file_read_block"},
    {15, 1, "vpp_native_file_close"},
    {16, 3, "vpp_native_link_macho"},
    {17, 3, "vpp_native_link_macho_modules"},
    {18, 2, "vpp_native_cache_read"},
    {19, 2, "vpp_native_cache_write"},
    {20, 3, "vpp_native_bytes_slice"},
    {21, 2, "vpp_native_bytes_find"},
    {22, 1, "vpp_native_bytes_print"},
    {23, 1, "vpp_native_file_open_write"},
    {24, 2, "vpp_native_file_write_block"},
    {25, 1, "vpp_native_memory_read_byte"},
    {26, 2, "vpp_native_memory_write_byte"},
    {27, 1, "vpp_native_memory_read_i32"},
    {28, 2, "vpp_native_memory_write_i32"},
    {29, 2, "vpp_native_memory_bind"},
    {30, 2, "vpp_native_os_open"},
    {31, 3, "vpp_native_os_read"},
    {32, 3, "vpp_native_os_write"},
    {33, 1, "vpp_native_os_close"},
    {34, 0, "vpp_native_os_errno"},
    {35, 1, "vpp_native_os_argument_length"},
    {36, 2, "vpp_native_os_argument_byte"},
    {37, 2, "vpp_native_i32_div"},
    {38, 0, "vpp_native_runtime_module_path"},
    {40, 0, "vpp_native_read_i32"},
    {41, 0, "vpp_native_read_status"},
    {42, 1, "vpp_native_file_read_byte"},
    {43, 2, "vpp_native_file_write_byte"},
    {44, 1, "vpp_native_os_home"},
    {45, 1, "vpp_native_os_cwd"},
    {46, 1, "vpp_native_os_mkstemp"},
    {47, 2, "vpp_native_os_rename"},
    {48, 1, "vpp_native_os_spawn_clang"},
    {49, 2, "vpp_native_os_waitpid"},
    {50, 2, "vpp_native_os_stat"},
    {51, 2, "vpp_native_os_lstat"},
    {52, 1, "vpp_native_os_access_exec"},
    {53, 1, "vpp_native_os_unlink"},
    {54, 2, "vpp_native_os_env_copy"},
    {55, 1, "vpp_native_os_executable_path"},
    {56, 1, "vpp_native_os_mkdir"},
    {57, 2, "vpp_native_os_fstat"},
    {58, 0, "vpp_native_sha_begin"},
    {59, 2, "vpp_native_sha_update"},
    {60, 1, "vpp_native_sha_end"},
    {61, 0, "vpp_native_os_register_cache_report"},
    {78, 2, "vpp_native_os_fchmod"},
}};

inline constexpr const NativeCoreAbiEntry *nativeCoreAbi(int id) noexcept {
    for (const auto &entry : kNativeCoreAbi)
        if (entry.id == id) return &entry;
    return nullptr;
}

inline constexpr int nativeCoreArity(int id) noexcept {
    const auto *entry = nativeCoreAbi(id);
    return entry ? entry->arity : -1;
}

} // namespace vietvm::native
