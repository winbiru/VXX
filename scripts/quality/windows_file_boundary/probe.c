// Native Windows runtime smoke test for the Windows x64 ASM file boundary.
// This tests actual Win32/UCRT behavior, beyond macOS cross-assembly.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <io.h>
#include <stdlib.h>

// Kích thước vùng nhớ của ranh giới Windows x64 đang được kiểm tra.
#define PROBE_ARENA_SIZE 135271424

extern unsigned char *vpp_native_memory_base(void);
extern int vpp_native_os_mkstemp(int);
extern int vpp_native_os_stat(int, int);
extern int vpp_native_os_lstat(int, int);
extern int vpp_native_os_fstat(int, int);
extern int vpp_native_os_rename(int, int);
extern int vpp_native_os_open(int, int);
extern int vpp_native_os_unlink(int);
extern int vpp_native_os_mkdir(int);
extern int vpp_native_os_access_exec(int);
extern int vpp_native_os_cwd(int);
extern int vpp_native_os_executable_path(int);
extern int vpp_native_os_env_copy(int, int);
extern int vpp_native_os_home(int);
extern int vpp_native_os_spawn_clang(int);
extern int vpp_native_os_waitpid(int, int);
extern int vpp_native_sha_begin(void);
extern int vpp_native_sha_update(int, int);
extern int vpp_native_sha_end(int);
extern int vpp_native_argc(void);
extern int vpp_native_os_argument_length(int);
extern int vpp_native_os_argument_byte(int, int);
void vpp_native_cache_report_entry(void) {} // unrelated atexit adapter
#ifdef __MINGW32__
// MinGW's legacy MSVCRT import archive omits __p___argc, which UCRT
// provides on native Windows. The Wine probe supplies that CRT shim.
extern int vpp_mingw_probe_argc;
#endif

#define REQUIRE(expr) do { if (!(expr)) { \
    fprintf(stderr, "Windows file boundary failed at line %d: %s (errno=%d, win32=%lu)\n", \
            __LINE__, #expr, errno, (unsigned long)GetLastError()); \
    return 1; \
} } while (0)

static uint64_t identity(const unsigned char *arena, size_t offset) {
    uint64_t result;
    memcpy(&result, arena + offset + 8, sizeof(result));
    return result;
}

static uint32_t volume(const unsigned char *arena, size_t offset) {
    uint32_t result;
    memcpy(&result, arena + offset, sizeof(result));
    return result;
}

static unsigned mode(const unsigned char *arena, size_t offset) {
    return (unsigned)arena[offset + 4] | ((unsigned)arena[offset + 5] << 8);
}

static int test_sha(unsigned char *arena) {
    static const unsigned char empty_digest[32] = {
        0xe3,0xb0,0xc4,0x42,0x98,0xfc,0x1c,0x14,0x9a,0xfb,0xf4,0xc8,0x99,0x6f,0xb9,0x24,
        0x27,0xae,0x41,0xe4,0x64,0x9b,0x93,0x4c,0xa4,0x95,0x99,0x1b,0x78,0x52,0xb8,0x55
    };
    static const unsigned char abc_digest[32] = {
        0xba,0x78,0x16,0xbf,0x8f,0x01,0xcf,0xea,0x41,0x41,0x40,0xde,0x5d,0xae,0x22,0x23,
        0xb0,0x03,0x61,0xa3,0x96,0x17,0x7a,0x9c,0xb4,0x10,0xff,0x61,0xf2,0x00,0x15,0xad
    };
    static const unsigned char million_a_digest[32] = {
        0xcd,0xc7,0x6e,0x5c,0x99,0x14,0xfb,0x92,0x81,0xa1,0xc7,0xe2,0x84,0xd7,0x3e,0x67,
        0xf1,0x80,0x9a,0x48,0xa4,0x97,0x20,0x0e,0x04,0x6d,0x39,0xcc,0xc7,0x11,0x2c,0xd0
    };
    REQUIRE(vpp_native_sha_update(4096, 0) == -1);
    REQUIRE(vpp_native_sha_end(3216) == -1);
    REQUIRE(vpp_native_sha_begin() == 0);
    REQUIRE(vpp_native_sha_begin() == -1);
    REQUIRE(vpp_native_sha_update(-1, 0) == -1);
    REQUIRE(vpp_native_sha_update(4096, -1) == -1);
    REQUIRE(vpp_native_sha_update((PROBE_ARENA_SIZE - 1), 2) == -1);
    REQUIRE(vpp_native_sha_update(4096, 0) == 0);
    REQUIRE(vpp_native_sha_end(3216) == 0);
    REQUIRE(memcmp(arena + 3216, empty_digest, 32) == 0);
    REQUIRE(vpp_native_sha_end(3216) == -1);
    REQUIRE(vpp_native_sha_begin() == 0);
    memcpy(arena + 4096, "abc", 3);
    REQUIRE(vpp_native_sha_update(4096, 1) == 0);
    REQUIRE(vpp_native_sha_update(4097, 2) == 0);
    REQUIRE(vpp_native_sha_end(3216) == 0);
    REQUIRE(memcmp(arena + 3216, abc_digest, 32) == 0);
    REQUIRE(vpp_native_sha_begin() == 0);
    memset(arena + 4096, 'a', 1000);
    for (int i = 0; i < 1000; ++i)
        REQUIRE(vpp_native_sha_update(4096, 1000) == 0);
    REQUIRE(vpp_native_sha_end((PROBE_ARENA_SIZE - 1)) == -1);
    REQUIRE(vpp_native_sha_end(3216) == 0);
    REQUIRE(memcmp(arena + 3216, million_a_digest, 32) == 0);
    return 0;
}

static int test_spawn(unsigned char *arena) {
    char executable[MAX_PATH], base[MAX_PATH], directory[MAX_PATH];
    char copied[MAX_PATH];
    const size_t argv_offset = 0x04001400;
    char **process_argv = (char **)(arena + argv_offset);
    DWORD path_size = GetModuleFileNameA(NULL, executable, MAX_PATH);
    REQUIRE(path_size > 0 && path_size < MAX_PATH);
    strcpy((char *)arena + 32768, executable);
    process_argv[0] = (char *)arena + 32768;
    process_argv[1] = (char *)arena + 36864;
    process_argv[2] = NULL;
    REQUIRE(vpp_native_os_waitpid(1, 3360) == -1);
    REQUIRE(vpp_native_os_spawn_clang(0) == -1);
    for (int code = 0; code <= 7; code += 7) {
        int child;
        sprintf((char *)arena + 36864, "--child-exit=%d", code);
        child = vpp_native_os_spawn_clang((int)argv_offset);
        REQUIRE(child == 1);
        REQUIRE(process_argv[0] == (char *)arena + 32768);
        REQUIRE(vpp_native_os_spawn_clang((int)argv_offset) == -1);
        REQUIRE(vpp_native_os_waitpid(child, 3360) == child);
        REQUIRE(*(int *)(arena + 3360) == (code << 8));
        REQUIRE(vpp_native_os_waitpid(child, 3360) == -1);
    }
    // Windows CRT must quote paths containing spaces without a shell.
    REQUIRE(GetTempPathA(MAX_PATH, base) > 0);
    REQUIRE(GetTempFileNameA(base, "vsp", 0, directory) != 0);
    REQUIRE(DeleteFileA(directory) != 0);
    REQUIRE(CreateDirectoryA(directory, NULL) != 0);
    REQUIRE(snprintf(copied, sizeof(copied), "%s\\child with spaces.exe", directory) > 0);
    REQUIRE(CopyFileA(executable, copied, FALSE) != 0);
    strcpy((char *)arena + 32768, copied);
    strcpy((char *)arena + 36864, "--child-exit=11");
    REQUIRE(vpp_native_os_spawn_clang((int)argv_offset) == 1);
    REQUIRE(process_argv[0] == (char *)arena + 32768);
    REQUIRE(vpp_native_os_waitpid(1, 3360) == 1);
    REQUIRE(*(int *)(arena + 3360) == (11 << 8));
    REQUIRE(DeleteFileA(copied) != 0);
    REQUIRE(RemoveDirectoryA(directory) != 0);
    strcpy((char *)arena + 32768, "vpp-executable-does-not-exist.exe");
    REQUIRE(vpp_native_os_spawn_clang((int)argv_offset) == -1);
    REQUIRE(process_argv[0] == (char *)arena + 32768);
    process_argv[1] = (char *)(arena + PROBE_ARENA_SIZE); // outside arena
    REQUIRE(vpp_native_os_spawn_clang((int)argv_offset) == -1);
    process_argv[0] = NULL;
    REQUIRE(vpp_native_os_spawn_clang((int)argv_offset) == -1);
    return 0;
}

// Check the actual wide Windows command line, including the CRT backslash
// quoting rules. A narrow CRT argv would lose some characters on ANSI pages.
static int validate_child_unicode_commandline(void) {
    static const wchar_t *expected[] = {
        L"--child-validate", L"tiếng Việt 𠮷", L"", L"hai từ có khoảng trắng",
        L"dấu \"ngoặc\"", L"kết thúc\\", L"dấu \\\" sát nhau"
    };
    int count = 0;
    int matched = 1;
    wchar_t **args = CommandLineToArgvW(GetCommandLineW(), &count);
    if (!args || count != 8) matched = 0;
    if (matched) {
        for (int i = 0; i < 7; ++i) {
            if (wcscmp(args[i + 1], expected[i]) != 0) {
                matched = 0;
                break;
            }
        }
    }
    if (!matched) {
        fprintf(stderr, "Unicode child arguments: count=%d\n", count);
        for (int i = 0; args && i < count; ++i) {
            fprintf(stderr, "  arg %d:", i);
            for (const wchar_t *p = args[i]; *p; ++p)
                fprintf(stderr, " %04x", (unsigned)*p);
            fputc('\n', stderr);
        }
    }
    if (args) LocalFree(args);
    return matched ? 0 : 126;
}

static int validate_child_long_backslashes(void) {
    int count = 0;
    int matched = 1;
    wchar_t **args = CommandLineToArgvW(GetCommandLineW(), &count);
    if (!args || count != 3 || wcslen(args[2]) != 4095) matched = 0;
    if (matched) {
        for (int i = 0; i < 4095; ++i) {
            if (args[2][i] != L'\\') {
                matched = 0;
                break;
            }
        }
    }
    if (args) LocalFree(args);
    return matched ? 0 : 126;
}

static int test_unicode_spawn(unsigned char *arena) {
    wchar_t executable[MAX_PATH], base[MAX_PATH], directory[MAX_PATH], copied[MAX_PATH];
    const size_t argv_offset = 0x04001400;
    char **process_argv = (char **)(arena + argv_offset);
    static const char *arguments[] = {
        "--child-validate", "tiếng Việt 𠮷", "", "hai từ có khoảng trắng",
        "dấu \"ngoặc\"", "kết thúc\\", "dấu \\\" sát nhau"
    };
    REQUIRE(GetModuleFileNameW(NULL, executable, MAX_PATH) > 0);
    REQUIRE(GetTempPathW(MAX_PATH, base) > 0);
    REQUIRE(GetTempFileNameW(base, L"vsp", 0, directory) != 0);
    REQUIRE(DeleteFileW(directory) != 0);
    REQUIRE(CreateDirectoryW(directory, NULL) != 0);
    REQUIRE(wcslen(directory) + 50 < MAX_PATH);
    REQUIRE(swprintf(copied, MAX_PATH, L"%ls\\con dấu 𠮷 với khoảng trắng.exe", directory) > 0);
    REQUIRE(CopyFileW(executable, copied, FALSE) != 0);
    REQUIRE(WideCharToMultiByte(CP_UTF8, 0, copied, -1, (char *)arena + 32768,
                                4096, NULL, NULL) != 0);
    process_argv[0] = (char *)arena + 32768;
    for (int i = 0; i < 7; ++i) {
        process_argv[i + 1] = (char *)arena + 36864 + i * 4096;
        strcpy(process_argv[i + 1], arguments[i]);
    }
    process_argv[8] = NULL;
    REQUIRE(vpp_native_os_spawn_clang((int)argv_offset) == 1);
    REQUIRE(process_argv[0] == (char *)arena + 32768);
    REQUIRE(process_argv[2] == (char *)arena + 40960);
    REQUIRE(vpp_native_os_waitpid(1, 3360) == 1);
    REQUIRE(*(int *)(arena + 3360) == 0);

    // A rejected argument must not mutate the caller's argv pointers or
    // leave a process handle that blocks the next valid spawn.
    memcpy(process_argv[2], "invalid-\xc3(", 11);
    REQUIRE(vpp_native_os_spawn_clang((int)argv_offset) == -1);
    REQUIRE(process_argv[2] == (char *)arena + 40960);
    strcpy(process_argv[2], "tiếng Việt 𠮷");
    REQUIRE(vpp_native_os_spawn_clang((int)argv_offset) == 1);
    REQUIRE(vpp_native_os_waitpid(1, 3360) == 1);
    REQUIRE(*(int *)(arena + 3360) == 0);

    // Maximum validated UTF-8 argument length, all trailing backslashes:
    // quoting expands it to more than 8192 bytes of UTF-16.
    strcpy(process_argv[1], "--child-long");
    memset(process_argv[2], '\\', 4095);
    process_argv[2][4095] = 0;
    process_argv[3] = NULL;
    REQUIRE(vpp_native_os_spawn_clang((int)argv_offset) == 1);
    REQUIRE(vpp_native_os_waitpid(1, 3360) == 1);
    REQUIRE(*(int *)(arena + 3360) == 0);
    REQUIRE(DeleteFileW(copied) != 0);
    REQUIRE(RemoveDirectoryW(directory) != 0);
    return 0;
}

// A Windows process receives UTF-16 argv, while the V++ compiler operates on
// UTF-8 paths.  Include a surrogate pair to catch lossy ANSI conversions.
static int test_unicode_arguments(int argc) {
    static const char unicode[] = "gói biên dịch có dấu 𠮷";
    int actual_length;
    REQUIRE(argc == 3);
    actual_length = vpp_native_os_argument_length(1);
    if (actual_length != (int)strlen(unicode)) {
        fprintf(stderr, "Unicode argument: expected %zu UTF-8 bytes, got %d; actual bytes:",
                strlen(unicode), actual_length);
        for (int i = 0; i < actual_length && i < 256; ++i)
            fprintf(stderr, " %02x", vpp_native_os_argument_byte(1, i));
        fputc('\n', stderr);
    }
    REQUIRE(actual_length == (int)strlen(unicode));
    for (int i = 0; i < (int)strlen(unicode); ++i)
        REQUIRE(vpp_native_os_argument_byte(1, i) == (unsigned char)unicode[i]);
    REQUIRE(vpp_native_os_argument_length(2) == 0);
    REQUIRE(vpp_native_os_argument_byte(2, 0) == -1);
    REQUIRE(vpp_native_os_argument_length(1) == (int)strlen(unicode));
    REQUIRE(vpp_native_os_argument_byte(1, 0) == (unsigned char)unicode[0]);
    REQUIRE(vpp_native_os_argument_byte(1, (int)strlen(unicode)) == -1);
    REQUIRE(vpp_native_os_argument_length(-1) == -1);
    return 0;
}

static int test_unicode_environment(unsigned char *arena) {
    static const char key[] = "VPP_UTF8_𠮷";
    static const char value[] = "thư mục có dấu 𠮷\\công cụ";
    wchar_t saved_home[4096];
    wchar_t oversized[4200];
    DWORD saved_length, saved_error;
    const int key_slot = 70000, value_slot = 74096;

    saved_length = GetEnvironmentVariableW(L"VPP_HOME", saved_home, 4096);
    saved_error = GetLastError();
    REQUIRE(saved_length < 4096);

    REQUIRE(SetEnvironmentVariableW(L"VPP_UTF8_𠮷", L"thư mục có dấu 𠮷\\công cụ") != 0);
    strcpy((char *)arena + key_slot, key);
    memset(arena + value_slot, 0xab, 4096);
    REQUIRE(vpp_native_os_env_copy(key_slot, value_slot) == (int)strlen(value));
    REQUIRE(strcmp((char *)arena + value_slot, value) == 0);

    REQUIRE(SetEnvironmentVariableW(L"VPP_HOME", L"C:\\gói biên dịch 𠮷\\runtime") != 0);
    REQUIRE(vpp_native_os_home(value_slot) == (int)strlen("C:\\gói biên dịch 𠮷\\runtime"));
    REQUIRE(strcmp((char *)arena + value_slot, "C:\\gói biên dịch 𠮷\\runtime") == 0);

    REQUIRE(SetEnvironmentVariableW(L"VPP_UTF8_𠮷", L"") != 0);
    REQUIRE(vpp_native_os_env_copy(key_slot, value_slot) == 0);
    REQUIRE(arena[value_slot] == 0);

    // The destination is 4096 UTF-8 bytes including NUL, independent of
    // how many UTF-16 code units Windows needs for the environment value.
    for (size_t i = 0; i < 4095; ++i) oversized[i] = L'a';
    oversized[4095] = 0;
    REQUIRE(SetEnvironmentVariableW(L"VPP_UTF8_𠮷", oversized) != 0);
    REQUIRE(vpp_native_os_env_copy(key_slot, value_slot) == 4095);
    REQUIRE(arena[value_slot + 4095] == 0);
    oversized[4095] = L'a';
    oversized[4096] = 0;
    REQUIRE(SetEnvironmentVariableW(L"VPP_UTF8_𠮷", oversized) != 0);
    REQUIRE(vpp_native_os_env_copy(key_slot, value_slot) == -1);
    for (size_t i = 0; i < 1365; ++i) oversized[i] = L'\x20ac';
    oversized[1365] = 0; // 1365 * 3 UTF-8 bytes + NUL
    REQUIRE(SetEnvironmentVariableW(L"VPP_UTF8_𠮷", oversized) != 0);
    REQUIRE(vpp_native_os_env_copy(key_slot, value_slot) == 4095);
    REQUIRE(arena[value_slot + 4095] == 0);
    oversized[1365] = L'\x20ac';
    oversized[1366] = 0;
    REQUIRE(SetEnvironmentVariableW(L"VPP_UTF8_𠮷", oversized) != 0);
    REQUIRE(vpp_native_os_env_copy(key_slot, value_slot) == -1);

    for (size_t i = 0; i < 4199; ++i) oversized[i] = L'a';
    oversized[4199] = 0;
    REQUIRE(SetEnvironmentVariableW(L"VPP_UTF8_𠮷", oversized) != 0);
    REQUIRE(vpp_native_os_env_copy(key_slot, value_slot) == -1);

    REQUIRE(SetEnvironmentVariableW(L"VPP_UTF8_𠮷", NULL) != 0);
    arena[value_slot] = '@';
    REQUIRE(vpp_native_os_env_copy(key_slot, value_slot) == -1);
    REQUIRE(arena[value_slot] == '@');
    memcpy(arena + key_slot, "invalid-\xc3(", 11);
    REQUIRE(vpp_native_os_env_copy(key_slot, value_slot) == -1);
    REQUIRE(arena[value_slot] == '@');
    REQUIRE(vpp_native_os_home((PROBE_ARENA_SIZE - 1)) == -1);
    REQUIRE(vpp_native_os_env_copy(key_slot, (PROBE_ARENA_SIZE - 1)) == -1);

    REQUIRE(SetEnvironmentVariableW(L"VPP_HOME",
            saved_length == 0 && saved_error != ERROR_SUCCESS ? NULL : saved_home) != 0);
    return 0;
}

static int test_unicode_files(unsigned char *arena, const char *directory) {
    char first[1024], second[1024], subdir[1024], templ[1024];
    wchar_t first_wide[1024];
    FILE *file;
    int fd;
    // Preserve the file paths stored by the surrounding baseline probe.
    const int source_slot = 40000, target_slot = 44096;
    const int directory_slot = 48192, invalid_slot = 52288;
    const int temporary_slot = 56384;
    REQUIRE(snprintf(first, sizeof(first), "%s\\gói biên dịch 𠮷.vi", directory) > 0);
    REQUIRE(snprintf(second, sizeof(second), "%s\\đã đổi tên 𠮷.vi", directory) > 0);
    REQUIRE(snprintf(subdir, sizeof(subdir), "%s\\thư mục có dấu", directory) > 0);
    REQUIRE(snprintf(templ, sizeof(templ), "%s\\tạm thời 𠮷-XXXXXX", directory) > 0);
    REQUIRE(MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, first, -1,
                                first_wide, 1024) != 0);
    file = _wfopen(first_wide, L"wb");
    REQUIRE(file != NULL);
    REQUIRE(fwrite("unicode", 1, 7, file) == 7);
    REQUIRE(fclose(file) == 0);
    strcpy((char *)arena + source_slot, first);
    strcpy((char *)arena + target_slot, second);
    strcpy((char *)arena + directory_slot, subdir);
    REQUIRE(vpp_native_os_stat(source_slot, 3376) == 0);
    fd = vpp_native_os_open(source_slot, 0);
    REQUIRE(fd >= 0);
    REQUIRE(_close(fd) == 0);
    REQUIRE(vpp_native_os_rename(source_slot, target_slot) == 0);
    REQUIRE(vpp_native_os_stat(target_slot, 3376) == 0);
    REQUIRE(vpp_native_os_access_exec(target_slot) == 0);
    REQUIRE(vpp_native_os_unlink(target_slot) == 0);
    REQUIRE(vpp_native_os_mkdir(directory_slot) == 0);
    REQUIRE(vpp_native_os_stat(directory_slot, 3376) == 0);
    REQUIRE((mode(arena, 3376) & 0xf000) == 0x4000);
    strcpy((char *)arena + temporary_slot, templ);
    fd = vpp_native_os_mkstemp(temporary_slot);
    REQUIRE(fd >= 0);
    REQUIRE(strstr((char *)arena + temporary_slot, "XXXXXX") == NULL);
    REQUIRE(vpp_native_os_stat(temporary_slot, 3376) == 0);
    REQUIRE(_close(fd) == 0);
    REQUIRE(vpp_native_os_unlink(temporary_slot) == 0);
    {
        wchar_t subdir_wide[1024];
        wchar_t original_wide[1024];
        REQUIRE(GetCurrentDirectoryW(1024, original_wide) > 0);
        REQUIRE(MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, subdir, -1,
                                    subdir_wide, 1024) != 0);
        REQUIRE(SetCurrentDirectoryW(subdir_wide) != 0);
        REQUIRE(vpp_native_os_cwd(60000) > 0);
        REQUIRE(strstr((char *)arena + 60000, "thư mục có dấu") != NULL);
        REQUIRE(vpp_native_os_cwd(60000) == (int)strlen((char *)arena + 60000));
        REQUIRE(SetCurrentDirectoryW(original_wide) != 0);
        REQUIRE(RemoveDirectoryW(subdir_wide) != 0);
    }
    // Bad UTF-8 must fail without corrupting the saved stack pointer.
    memcpy(arena + invalid_slot, "invalid-\xc3(", 11);
    REQUIRE(vpp_native_os_open(invalid_slot, 0) == -1);
    REQUIRE(vpp_native_os_stat(invalid_slot, 3376) == -1);
    REQUIRE(vpp_native_os_rename(invalid_slot, target_slot) == -1);
    REQUIRE(vpp_native_os_unlink(invalid_slot) == -1);
    REQUIRE(vpp_native_os_mkdir(invalid_slot) == -1);
    REQUIRE(vpp_native_os_access_exec(invalid_slot) == -1);
    REQUIRE(vpp_native_os_cwd((PROBE_ARENA_SIZE - 1)) == -1);
    REQUIRE(vpp_native_os_executable_path((PROBE_ARENA_SIZE - 1)) == -1);
    return 0;
}

int main(int argc, char **argv) {
    char base[MAX_PATH], directory[MAX_PATH], templ[MAX_PATH];
    char second[MAX_PATH], hardlink[MAX_PATH], published[MAX_PATH];
    unsigned char *arena;
    int first_fd, second_fd;

    // Spawned probes must never re-enter the parent test suite if Windows or
    // Wine changes argument quoting: that would recursively spawn children.
    if (GetEnvironmentVariableA("VPP_WINDOWS_BOUNDARY_CHILD", NULL, 0) != 0) {
        if (argc == 2 && strncmp(argv[1], "--child-exit=", 13) == 0)
            return atoi(argv[1] + 13);
        if (argc >= 2 && strcmp(argv[1], "--child-validate") == 0)
            return validate_child_unicode_commandline();
        if (argc >= 2 && strcmp(argv[1], "--child-long") == 0)
            return validate_child_long_backslashes();
        fprintf(stderr, "Unexpected child arguments: argc=%d command=%s\n",
                argc, GetCommandLineA());
        return 125;
    }

#ifdef __MINGW32__
    vpp_mingw_probe_argc = argc;
#endif
    REQUIRE(vpp_native_argc() == argc);
    REQUIRE(vpp_native_os_argument_length(0) == (int)strlen(argv[0]));
    REQUIRE(vpp_native_os_argument_byte(0, 0) == (unsigned char)argv[0][0]);
    REQUIRE(vpp_native_os_argument_byte(0, (int)strlen(argv[0])) == -1);
    REQUIRE(vpp_native_os_argument_length(argc) == -1);
    REQUIRE(vpp_native_os_argument_byte(-1, 0) == -1);
    REQUIRE(test_unicode_arguments(argc) == 0);

    REQUIRE(GetTempPathA(MAX_PATH, base) > 0);
    REQUIRE(GetTempFileNameA(base, "vpp", 0, directory) != 0);
    REQUIRE(DeleteFileA(directory) != 0);
    REQUIRE(CreateDirectoryA(directory, NULL) != 0);
    REQUIRE(snprintf(templ, sizeof(templ), "%s\\new-XXXXXX", directory) > 0);
    REQUIRE(snprintf(second, sizeof(second), "%s\\second-XXXXXX", directory) > 0);
    REQUIRE(snprintf(hardlink, sizeof(hardlink), "%s\\hardlink", directory) > 0);
    REQUIRE(snprintf(published, sizeof(published), "%s\\published", directory) > 0);

    arena = vpp_native_memory_base();
    REQUIRE(arena != NULL);
    REQUIRE(test_unicode_environment(arena) == 0);
    {
        wchar_t wide_executable[4096];
        char utf8_executable[4096];
        REQUIRE(GetModuleFileNameW(NULL, wide_executable, 4096) > 0);
        REQUIRE(WideCharToMultiByte(CP_UTF8, 0, wide_executable, -1,
                                     utf8_executable, 4096, NULL, NULL) > 0);
        REQUIRE(vpp_native_os_executable_path(64000) == (int)strlen(utf8_executable));
        REQUIRE(strcmp((char *)arena + 64000, utf8_executable) == 0);
    }
    REQUIRE(test_sha(arena) == 0);
    REQUIRE(SetEnvironmentVariableA("VPP_WINDOWS_BOUNDARY_CHILD", "1") != 0);
    REQUIRE(test_spawn(arena) == 0);
    REQUIRE(test_unicode_spawn(arena) == 0);
    REQUIRE(SetEnvironmentVariableA("VPP_WINDOWS_BOUNDARY_CHILD", NULL) != 0);
    strcpy((char *)arena + 4096, templ);
    first_fd = vpp_native_os_mkstemp(4096);
    REQUIRE(first_fd >= 0);
    REQUIRE(strstr((char *)arena + 4096, "XXXXXX") == NULL);
    REQUIRE(vpp_native_os_stat(4096, 3376) == 0);
    REQUIRE((mode(arena, 3376) & 0xf000) == 0x8000);
    REQUIRE(vpp_native_os_fstat(first_fd, 3584) == 0);
    REQUIRE(volume(arena, 3376) == volume(arena, 3584));
    REQUIRE(identity(arena, 3376) == identity(arena, 3584));
    REQUIRE(CreateHardLinkA(hardlink, (char *)arena + 4096, NULL) != 0);
    strcpy((char *)arena + 8192, hardlink);
    REQUIRE(vpp_native_os_lstat(8192, 3584) == 0);
    REQUIRE(volume(arena, 3376) == volume(arena, 3584));
    REQUIRE(identity(arena, 3376) == identity(arena, 3584));
    REQUIRE(_close(first_fd) == 0);

    strcpy((char *)arena + 12288, second);
    second_fd = vpp_native_os_mkstemp(12288);
    REQUIRE(second_fd >= 0);
    REQUIRE(_write(second_fd, "new", 3) == 3);
    REQUIRE(_close(second_fd) == 0);
    { FILE *destination = fopen(published, "wb");
      REQUIRE(destination != NULL);
      REQUIRE(fwrite("old", 1, 3, destination) == 3);
      REQUIRE(fclose(destination) == 0);
    }
    strcpy((char *)arena + 16384, published);
    REQUIRE(vpp_native_os_rename(12288, 16384) == 0);
    // Failed publication must leave the existing executable untouched.
    strcpy((char *)arena + 20480, "vpp-source-does-not-exist");
    REQUIRE(vpp_native_os_rename(20480, 16384) == -1);
    { char content[4] = {0};
      FILE *destination = fopen(published, "rb");
      REQUIRE(destination != NULL);
      REQUIRE(fread(content, 1, 3, destination) == 3);
      REQUIRE(strcmp(content, "new") == 0);
      REQUIRE(fclose(destination) == 0);
    }

    REQUIRE(vpp_native_os_stat(16384, 3376) == 0);
    strcpy((char *)arena + 20480, "invalid-no-X-suffix");
    REQUIRE(vpp_native_os_mkstemp(20480) == -1);
    REQUIRE(vpp_native_os_fstat(-1, 3376) == -1);
    REQUIRE(vpp_native_os_stat(20480, 3376) == -1);
    REQUIRE(errno == ENOENT);
    strcpy((char *)arena + 24576, directory);
    REQUIRE(vpp_native_os_lstat(24576, 3376) == 0);
    REQUIRE((mode(arena, 3376) & 0xf000) == 0x4000);
    REQUIRE(test_unicode_files(arena, directory) == 0);

    REQUIRE(DeleteFileA(hardlink) != 0);
    REQUIRE(DeleteFileA((char *)arena + 4096) != 0);
    REQUIRE(DeleteFileA(published) != 0);
    REQUIRE(RemoveDirectoryA(directory) != 0);
    puts("WINDOWS_NATIVE_FILE_PROCESS_SHA=PASS");
    return 0;
}
