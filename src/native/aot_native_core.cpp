#include "vpp/native/aot_runtime.h"

#include <cstdint>

#include "vpp/native/core_abi.h"
#include "vpp/runtime/error.h"

namespace vietvm::native::aot {
namespace {

[[noreturn]] void failNativeCore(const char *message) {
    throw runtime::RuntimeError(std::string("Native AOT: ") + message,
                                runtime::RuntimeErrorKind::CallBoundary);
}

extern "C" {
std::int32_t vpp_native_bytes_new(void);
std::int32_t vpp_native_bytes_length(std::int32_t);
std::int32_t vpp_native_bytes_get(std::int32_t,std::int32_t);
std::int32_t vpp_native_bytes_append(std::int32_t,std::int32_t);
std::int32_t vpp_native_bytes_push_i32(std::int32_t,std::int32_t);
std::int32_t vpp_native_bytes_load_i32(std::int32_t,std::int32_t);
std::int32_t vpp_native_bytes_store_i32(std::int32_t,std::int32_t,std::int32_t);
std::int32_t vpp_native_bytes_clear(std::int32_t);
std::int32_t vpp_native_bytes_concat(std::int32_t,std::int32_t);
std::int32_t vpp_native_bytes_equal(std::int32_t,std::int32_t);
std::int32_t vpp_native_bytes_free(std::int32_t);
std::int32_t vpp_native_argc(void);
std::int32_t vpp_native_argv(std::int32_t);
std::int32_t vpp_native_file_open_read(std::int32_t);
std::int32_t vpp_native_file_read_block(std::int32_t,std::int32_t);
std::int32_t vpp_native_file_close(std::int32_t);
std::int32_t vpp_native_link_macho(std::int32_t,std::int32_t,std::int32_t);
std::int32_t vpp_native_link_macho_modules(std::int32_t,std::int32_t,std::int32_t);
std::int32_t vpp_native_cache_read(std::int32_t,std::int32_t);
std::int32_t vpp_native_cache_write(std::int32_t,std::int32_t);
std::int32_t vpp_native_bytes_slice(std::int32_t,std::int32_t,std::int32_t);
std::int32_t vpp_native_bytes_find(std::int32_t,std::int32_t);
std::int32_t vpp_native_bytes_print(std::int32_t);
std::int32_t vpp_native_file_open_write(std::int32_t);
std::int32_t vpp_native_file_write_block(std::int32_t,std::int32_t);
std::int32_t vpp_native_memory_read_byte(std::int32_t);
std::int32_t vpp_native_memory_write_byte(std::int32_t,std::int32_t);
std::int32_t vpp_native_memory_read_i32(std::int32_t);
std::int32_t vpp_native_memory_write_i32(std::int32_t,std::int32_t);
std::int32_t vpp_native_memory_bind(std::int32_t,std::int32_t);
std::int32_t vpp_native_os_open(std::int32_t,std::int32_t);
std::int32_t vpp_native_os_read(std::int32_t,std::int32_t,std::int32_t);
std::int32_t vpp_native_os_write(std::int32_t,std::int32_t,std::int32_t);
std::int32_t vpp_native_os_close(std::int32_t);
std::int32_t vpp_native_os_errno(void);
std::int32_t vpp_native_os_argument_length(std::int32_t);
std::int32_t vpp_native_os_argument_byte(std::int32_t,std::int32_t);
std::int32_t vpp_native_i32_div(std::int32_t,std::int32_t);
std::int32_t vpp_native_runtime_module_path(void);
std::int32_t vpp_native_read_i32(void);
std::int32_t vpp_native_read_status(void);
std::int32_t vpp_native_file_read_byte(std::int32_t);
std::int32_t vpp_native_file_write_byte(std::int32_t,std::int32_t);
std::int32_t vpp_native_os_home(std::int32_t);
std::int32_t vpp_native_os_cwd(std::int32_t);
std::int32_t vpp_native_os_mkstemp(std::int32_t);
std::int32_t vpp_native_os_rename(std::int32_t,std::int32_t);
std::int32_t vpp_native_os_spawn_clang(std::int32_t);
std::int32_t vpp_native_os_waitpid(std::int32_t,std::int32_t);
std::int32_t vpp_native_os_stat(std::int32_t,std::int32_t);
std::int32_t vpp_native_os_lstat(std::int32_t,std::int32_t);
std::int32_t vpp_native_os_access_exec(std::int32_t);
std::int32_t vpp_native_os_unlink(std::int32_t);
std::int32_t vpp_native_os_env_copy(std::int32_t,std::int32_t);
std::int32_t vpp_native_os_executable_path(std::int32_t);
std::int32_t vpp_native_os_mkdir(std::int32_t);
std::int32_t vpp_native_os_fstat(std::int32_t,std::int32_t);
std::int32_t vpp_native_sha_begin(void);
std::int32_t vpp_native_sha_update(std::int32_t,std::int32_t);
std::int32_t vpp_native_sha_end(std::int32_t);
std::int32_t vpp_native_os_register_cache_report(void);
std::int32_t vpp_native_os_fchmod(std::int32_t,std::int32_t);
}

int callNativeCore(int primitive, int a, int b, int c) {
#define VPP_CORE_CALL0(id, fn) case id: return fn()
#define VPP_CORE_CALL1(id, fn) case id: return fn(a)
#define VPP_CORE_CALL2(id, fn) case id: return fn(a,b)
#define VPP_CORE_CALL3(id, fn) case id: return fn(a,b,c)
    switch (primitive) {
        VPP_CORE_CALL0(0, vpp_native_bytes_new);
        VPP_CORE_CALL1(1, vpp_native_bytes_length);
        VPP_CORE_CALL2(2, vpp_native_bytes_get);
        VPP_CORE_CALL2(3, vpp_native_bytes_append);
        VPP_CORE_CALL2(4, vpp_native_bytes_push_i32);
        VPP_CORE_CALL2(5, vpp_native_bytes_load_i32);
        VPP_CORE_CALL3(6, vpp_native_bytes_store_i32);
        VPP_CORE_CALL1(7, vpp_native_bytes_clear);
        VPP_CORE_CALL2(8, vpp_native_bytes_concat);
        VPP_CORE_CALL2(9, vpp_native_bytes_equal);
        VPP_CORE_CALL1(10, vpp_native_bytes_free);
        VPP_CORE_CALL0(11, vpp_native_argc);
        VPP_CORE_CALL1(12, vpp_native_argv);
        VPP_CORE_CALL1(13, vpp_native_file_open_read);
        VPP_CORE_CALL2(14, vpp_native_file_read_block);
        VPP_CORE_CALL1(15, vpp_native_file_close);
        VPP_CORE_CALL3(16, vpp_native_link_macho);
        VPP_CORE_CALL3(17, vpp_native_link_macho_modules);
        VPP_CORE_CALL2(18, vpp_native_cache_read);
        VPP_CORE_CALL2(19, vpp_native_cache_write);
        VPP_CORE_CALL3(20, vpp_native_bytes_slice);
        VPP_CORE_CALL2(21, vpp_native_bytes_find);
        VPP_CORE_CALL1(22, vpp_native_bytes_print);
        VPP_CORE_CALL1(23, vpp_native_file_open_write);
        VPP_CORE_CALL2(24, vpp_native_file_write_block);
        VPP_CORE_CALL1(25, vpp_native_memory_read_byte);
        VPP_CORE_CALL2(26, vpp_native_memory_write_byte);
        VPP_CORE_CALL1(27, vpp_native_memory_read_i32);
        VPP_CORE_CALL2(28, vpp_native_memory_write_i32);
        VPP_CORE_CALL2(29, vpp_native_memory_bind);
        VPP_CORE_CALL2(30, vpp_native_os_open);
        VPP_CORE_CALL3(31, vpp_native_os_read);
        VPP_CORE_CALL3(32, vpp_native_os_write);
        VPP_CORE_CALL1(33, vpp_native_os_close);
        VPP_CORE_CALL0(34, vpp_native_os_errno);
        VPP_CORE_CALL1(35, vpp_native_os_argument_length);
        VPP_CORE_CALL2(36, vpp_native_os_argument_byte);
        VPP_CORE_CALL2(37, vpp_native_i32_div);
        VPP_CORE_CALL0(38, vpp_native_runtime_module_path);
        VPP_CORE_CALL0(40, vpp_native_read_i32);
        VPP_CORE_CALL0(41, vpp_native_read_status);
        VPP_CORE_CALL1(42, vpp_native_file_read_byte);
        VPP_CORE_CALL2(43, vpp_native_file_write_byte);
        VPP_CORE_CALL1(44, vpp_native_os_home);
        VPP_CORE_CALL1(45, vpp_native_os_cwd);
        VPP_CORE_CALL1(46, vpp_native_os_mkstemp);
        VPP_CORE_CALL2(47, vpp_native_os_rename);
        VPP_CORE_CALL1(48, vpp_native_os_spawn_clang);
        VPP_CORE_CALL2(49, vpp_native_os_waitpid);
        VPP_CORE_CALL2(50, vpp_native_os_stat);
        VPP_CORE_CALL2(51, vpp_native_os_lstat);
        VPP_CORE_CALL1(52, vpp_native_os_access_exec);
        VPP_CORE_CALL1(53, vpp_native_os_unlink);
        VPP_CORE_CALL2(54, vpp_native_os_env_copy);
        VPP_CORE_CALL1(55, vpp_native_os_executable_path);
        VPP_CORE_CALL1(56, vpp_native_os_mkdir);
        VPP_CORE_CALL2(57, vpp_native_os_fstat);
        VPP_CORE_CALL0(58, vpp_native_sha_begin);
        VPP_CORE_CALL2(59, vpp_native_sha_update);
        VPP_CORE_CALL1(60, vpp_native_sha_end);
        VPP_CORE_CALL0(61, vpp_native_os_register_cache_report);
        VPP_CORE_CALL2(78, vpp_native_os_fchmod);
        default: failNativeCore("native core primitive id");
    }
#undef VPP_CORE_CALL0
#undef VPP_CORE_CALL1
#undef VPP_CORE_CALL2
#undef VPP_CORE_CALL3
}

void runNativeCore(Frame &frame, int primitive, int count) {
    frame.nativeCore(primitive, count);
}

} // namespace

void Frame::nativeCore(int primitive, int count) {
    const auto *abi = nativeCoreAbi(primitive);
    if (!abi || count != abi->arity) failNativeCore("native core arity");
    auto args = take(count);
    int raw[3] = {0, 0, 0};
    for (int i = 0; i < count; ++i) {
        const auto *value = std::get_if<int>(&args[static_cast<std::size_t>(i)]);
        if (!value) failNativeCore("native core requires i32 arguments");
        raw[i] = *value;
    }
    push(callNativeCore(primitive, raw[0], raw[1], raw[2]));
}

extern "C" int vpp_aot_native_core(int id, int primitive, int count) noexcept {
    return runAotBoundary2(id, primitive, count, runNativeCore);
}

} // namespace vietvm::native::aot
