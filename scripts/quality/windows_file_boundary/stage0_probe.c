// Exercise the C bootstrap and Win64 machine ABI in the same executable.
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>

extern int vpp_windows_stage0_abi(void);
extern int32_t vpp_native_argc(void);
extern int32_t vpp_native_argv(int32_t);
extern int32_t vpp_native_os_argument_length(int32_t);
extern int32_t vpp_native_os_argument_byte(int32_t, int32_t);
extern int32_t vpp_native_bytes_new(void);
extern int32_t vpp_native_bytes_append(int32_t, int32_t);
extern int32_t vpp_native_bytes_length(int32_t);
extern int32_t vpp_native_bytes_get(int32_t, int32_t);
extern int32_t vpp_native_bytes_free(int32_t);
extern int32_t vpp_native_file_open_read(int32_t);
extern int32_t vpp_native_file_open_write(int32_t);
extern int32_t vpp_native_file_read_block(int32_t, int32_t);
extern int32_t vpp_native_file_write_block(int32_t, int32_t);
extern int32_t vpp_native_file_close(int32_t);
extern int32_t vpp_native_runtime_module_path(void);
extern int32_t vpp_native_memory_write_i32(int32_t, int32_t);
extern int32_t vpp_native_memory_read_i32(int32_t);
extern int32_t vpp_native_i32_div(int32_t, int32_t);

#define CHECK(expr) do { if (!(expr)) { \
    fprintf(stderr, "Windows Stage 0 line %d: %s\n", __LINE__, #expr); \
    return 1; \
} } while (0)

static int matches(int32_t handle, const char *text) {
    size_t length = strlen(text);
    if (vpp_native_bytes_length(handle) != (int32_t)length) return 0;
    for (size_t i = 0; i < length; ++i) {
        if (vpp_native_bytes_get(handle, (int32_t)i) != (unsigned char)text[i]) return 0;
    }
    return 1;
}

int main(void) {
    CHECK(vpp_windows_stage0_abi() == 0);
    const char *argument = "gói biên dịch có dấu 𠮷";
    CHECK(vpp_native_argc() == 3);
    int32_t unicode = vpp_native_argv(1);
    int32_t empty = vpp_native_argv(2);
    CHECK(matches(unicode, argument));
    CHECK(matches(empty, ""));
    CHECK(vpp_native_argv(-1) == -1);
    CHECK(vpp_native_argv(3) == -1);
    CHECK(vpp_native_os_argument_length(1) == (int32_t)strlen(argument));
    CHECK(vpp_native_os_argument_length(2) == 0);
    CHECK(vpp_native_os_argument_byte(2, 0) == -1);
    CHECK(vpp_native_os_argument_byte(1, (int32_t)strlen(argument)) == -1);
    CHECK(vpp_native_bytes_free(unicode) == 0);
    CHECK(vpp_native_bytes_free(empty) == 0);

    CHECK(vpp_native_memory_write_i32(4096, INT32_MIN) == 0);
    CHECK(vpp_native_memory_read_i32(4096) == INT32_MIN);
    CHECK(vpp_native_i32_div(-159, -3) == 53);

    int32_t module = vpp_native_runtime_module_path();
    CHECK(module > 0);
    int32_t file = vpp_native_file_open_read(module);
    CHECK(file > 0);
    int32_t content = vpp_native_bytes_new();
    CHECK(content > 0);
    CHECK(vpp_native_file_read_block(file, content) > 0);
    CHECK(vpp_native_file_close(file) == 0);
    CHECK(vpp_native_bytes_free(module) == 0);
    CHECK(vpp_native_bytes_free(content) == 0);

    const char *filename = "stage0-tệp 𠮷.bin";
    const unsigned char data[] = {0, 255, 10, 52};
    int32_t path = vpp_native_bytes_new();
    content = vpp_native_bytes_new();
    CHECK(path > 0 && content > 0);
    for (size_t i = 0; i < strlen(filename); ++i)
        CHECK(vpp_native_bytes_append(path, (unsigned char)filename[i]) == 0);
    for (size_t i = 0; i < sizeof data; ++i)
        CHECK(vpp_native_bytes_append(content, data[i]) == 0);
    file = vpp_native_file_open_write(path);
    CHECK(file > 0);
    CHECK(vpp_native_file_write_block(file, content) == (int32_t)sizeof data);
    CHECK(vpp_native_file_close(file) == 0);
    CHECK(vpp_native_bytes_free(content) == 0);
    content = vpp_native_bytes_new();
    file = vpp_native_file_open_read(path);
    CHECK(file > 0);
    CHECK(vpp_native_file_read_block(file, content) == (int32_t)sizeof data);
    for (size_t i = 0; i < sizeof data; ++i)
        CHECK(vpp_native_bytes_get(content, (int32_t)i) == data[i]);
    CHECK(vpp_native_file_close(file) == 0);
    CHECK(vpp_native_bytes_append(path, 0) == 0);
    CHECK(vpp_native_file_open_write(path) == -1);
    CHECK(vpp_native_bytes_free(path) == 0);
    CHECK(vpp_native_bytes_free(content) == 0);
    CHECK(_wremove(L"stage0-tệp 𠮷.bin") == 0);
    puts("Windows C bootstrap + machine primitives: PASS");
    return 0;
}
