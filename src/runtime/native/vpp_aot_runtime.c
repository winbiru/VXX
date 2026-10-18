// Legacy C runtime for native AOT targets without the V++/assembly runtime.
// When VPP_NATIVE_VPP_RUNTIME is selected, runtime.vi and the native machine
// primitives provide these symbols. Leave the old implementation here for
// bootstrap/fallback builds, but compile none of it into a V++ runtime binary.
// This also retires the former hybrid C cache/link services on that path.
#if !defined(VPP_NATIVE_VPP_RUNTIME)
#include <inttypes.h>
#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <string.h>
#include <stddef.h>
#include <fcntl.h>
#include <spawn.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#ifdef __APPLE__
#include <CommonCrypto/CommonDigest.h>
#include <crt_externs.h>
#include <mach-o/dyld.h>
#elif defined(__linux__)
#include <openssl/sha.h>
// Use the same SHA-256 cache format on both POSIX native hosts.
#define CC_SHA256_CTX SHA256_CTX
#define CC_SHA256_DIGEST_LENGTH SHA256_DIGEST_LENGTH
#define CC_SHA256_Init SHA256_Init
#define CC_SHA256_Update SHA256_Update
#define CC_SHA256_Final SHA256_Final
#define CC_LONG size_t
#endif

#ifndef VPP_NATIVE_VPP_RUNTIME
int32_t vpp_native_print_i32(int32_t value) {
    return printf("%" PRId32 "\n", value) < 0 ? -1 : 0;
}

// A minimal input ABI while native AOT only supports signed i32 values.
// A separate status is required because every i32 value is valid input.
// Status: 0 = OK, 1 = EOF, 2 = invalid input, 3 = out of i32 range.
static int32_t vpp_native_input_status = 0;

int32_t vpp_native_read_i32(void) {
    char buffer[64];
    char *end;
    long value;
    int ch;
    vpp_native_input_status = 0;
    if (!fgets(buffer, sizeof buffer, stdin)) {
        vpp_native_input_status = feof(stdin) ? 1 : 2;
        return 0;
    }
    // Reject an overlong line without accidentally interpreting its prefix.
    size_t length = 0;
    while (buffer[length] != '\0') ++length;
    if (length > 0 && buffer[length - 1] != '\n' && !feof(stdin)) {
        while ((ch = getchar()) != '\n' && ch != EOF) {}
        vpp_native_input_status = 2;
        return 0;
    }
    errno = 0;
    value = strtol(buffer, &end, 10);
    if (end == buffer) {
        vpp_native_input_status = 2;
        return 0;
    }
    while (*end != '\0' && isspace((unsigned char)*end)) ++end;
    if (*end != '\0') {
        vpp_native_input_status = 2;
        return 0;
    }
    if (errno == ERANGE || value < INT32_MIN || value > INT32_MAX) {
        vpp_native_input_status = 3;
        return 0;
    }
    return (int32_t)value;
}

int32_t vpp_native_read_status(void) {
    return vpp_native_input_status;
}
#endif

// Handles are monotonically issued and type-checked. A closed handle can
// never accidentally refer to a newly opened file/buffer. No C pointer is
// exposed through the current signed-i32 Machine IR ABI.
#define VPP_NATIVE_BYTES_SLOTS 64
#define VPP_NATIVE_FILE_SLOTS 64
#define VPP_NATIVE_BYTES_MAX (1024u * 1024u)
#define VPP_NATIVE_PATH_MAX 4096u
#define VPP_NATIVE_IO_CHUNK 4096u

typedef struct {
    int32_t id;
    unsigned char *data;
    size_t length;
    size_t capacity;
} vpp_native_bytes_slot;

typedef struct {
    int32_t id;
    FILE *stream;
} vpp_native_file_slot;

#ifdef VPP_NATIVE_VPP_RUNTIME
// The .vi runtime owns this metadata. C compiler services only borrow views
// for cache/link I/O; no C buffer, handle or file algorithms are compiled.
extern unsigned char *vpp_native_memory_base(void);
extern int32_t vpp_native_bytes_new(void);
extern int32_t vpp_native_bytes_free(int32_t);
extern int32_t vpp_native_bytes_clear(int32_t);
extern int32_t vpp_native_bytes_concat(int32_t, int32_t);
extern int32_t vpp_native_bytes_push_i32(int32_t, int32_t);
extern int32_t vpp_native_bytes_reserve_extra(int32_t, int32_t);
int32_t vpp_native_link_macho_modules(int32_t, int32_t, int32_t);
extern int32_t vpp_native_object_services(int32_t, int32_t, int32_t);
extern int32_t vpp_native_object_archive_entry(int32_t, int32_t);
extern int32_t vpp_native_object_archive_summary(int32_t);
extern int32_t vpp_native_link_archive_single(int32_t);
extern int32_t vpp_native_link_validate_paths(int32_t, int32_t);
extern int32_t vpp_native_link_temporary_path(int32_t, int32_t, int32_t);
extern int32_t vpp_native_link_temp_open(int32_t);
extern int32_t vpp_native_link_publish(int32_t, int32_t);
extern int32_t vpp_native_link_spawn_wait(void);
extern int32_t vpp_native_link_validate_files(int32_t, int32_t);
extern int32_t vpp_native_link_executable_ready(int32_t);
extern int32_t vpp_native_link_write_object(int32_t, int32_t, int32_t);
extern int32_t vpp_native_runtime_file_path(int32_t);
extern int32_t vpp_native_cache_header_write(int32_t, int32_t);
extern int32_t vpp_native_cache_header_check(int32_t, int32_t);
extern int32_t vpp_native_cache_digest_hex(void);
_Static_assert(sizeof(vpp_native_bytes_slot) == 32, "runtime byte record ABI");
_Static_assert(offsetof(vpp_native_bytes_slot, data) == 8, "64-bit data pointer ABI");
_Static_assert(offsetof(vpp_native_bytes_slot, length) == 16, "runtime length ABI");
_Static_assert(offsetof(vpp_native_bytes_slot, capacity) == 24, "runtime capacity ABI");
#ifdef __APPLE__
// The V++ filesystem policy reads the Darwin arm64 stat fields from two
// reserved arena windows. Fail at build time if the OS ABI ever changes.
_Static_assert(sizeof(dev_t) == 4, "Darwin file device ABI");
_Static_assert(sizeof(ino_t) == 8, "Darwin 64-bit inode ABI");
_Static_assert(sizeof(mode_t) == 2, "Darwin file mode ABI");
_Static_assert(offsetof(struct stat, st_dev) == 0, "Darwin device field offset");
_Static_assert(offsetof(struct stat, st_mode) == 4, "Darwin mode field offset");
_Static_assert(offsetof(struct stat, st_ino) == 8, "Darwin inode field offset");
_Static_assert(sizeof(struct stat) <= 208, "Darwin stat scratch window");
#endif
#else
static vpp_native_bytes_slot vpp_native_bytes[VPP_NATIVE_BYTES_SLOTS];
static vpp_native_file_slot vpp_native_files[VPP_NATIVE_FILE_SLOTS];
static int32_t vpp_native_next_handle = 1;

static int32_t vpp_native_allocate_handle(void) {
    if (vpp_native_next_handle == INT32_MAX) return -1;
    return vpp_native_next_handle++;
}
#endif

static vpp_native_bytes_slot *vpp_native_find_bytes(int32_t handle) {
    if (handle <= 0) return NULL;
#ifdef VPP_NATIVE_VPP_RUNTIME
    unsigned char *arena = vpp_native_memory_base();
    if (!arena) return NULL;
    vpp_native_bytes_slot *vpp_native_bytes = (void *)(arena + 16);
#endif
    for (size_t i = 0; i < VPP_NATIVE_BYTES_SLOTS; ++i) {
        if (vpp_native_bytes[i].id == handle) return &vpp_native_bytes[i];
    }
    return NULL;
}

#ifndef VPP_NATIVE_VPP_RUNTIME
static vpp_native_file_slot *vpp_native_find_file(int32_t handle) {
    if (handle <= 0) return NULL;
    for (size_t i = 0; i < VPP_NATIVE_FILE_SLOTS; ++i) {
        if (vpp_native_files[i].id == handle) return &vpp_native_files[i];
    }
    return NULL;
}
#endif

// Reserve enough space for additional bytes plus a trailing NUL. Do not
// mutate the buffer on allocation failure or exceed the fixed 1 MiB cap.
static int vpp_native_bytes_reserve(vpp_native_bytes_slot *slot, size_t additional) {
    if (!slot || additional > VPP_NATIVE_BYTES_MAX - slot->length) return -1;
#ifdef VPP_NATIVE_VPP_RUNTIME
    return vpp_native_bytes_reserve_extra(slot->id, (int32_t)additional);
#else
    const size_t required = slot->length + additional + 1;
    if (required <= slot->capacity) return 0;
    size_t next = slot->capacity;
    while (next < required) {
        if (next > (VPP_NATIVE_BYTES_MAX + 1u) / 2u) {
            next = VPP_NATIVE_BYTES_MAX + 1u;
            break;
        }
        next *= 2;
    }
    unsigned char *grown = (unsigned char *)realloc(slot->data, next);
    if (!grown) return -1;
    slot->data = grown;
    slot->capacity = next;
    return 0;
#endif
}

#ifndef VPP_NATIVE_VPP_RUNTIME
int32_t vpp_native_bytes_new(void) {
    for (size_t i = 0; i < VPP_NATIVE_BYTES_SLOTS; ++i) {
        vpp_native_bytes_slot *slot = &vpp_native_bytes[i];
        if (slot->id != 0) continue;
        const int32_t id = vpp_native_allocate_handle();
        if (id < 0) return -1;
        unsigned char *data = (unsigned char *)malloc(1);
        if (!data) return -1;
        data[0] = 0;
        slot->data = data;
        slot->capacity = 1;
        slot->length = 0;
        slot->id = id;
        return id;
    }
    return -1;
}

// Append raw bytes, including NUL. Buffer is always NUL-terminated to make
// conversion to a checked file path safe; length counts data only.
int32_t vpp_native_bytes_append(int32_t handle, int32_t value) {
    vpp_native_bytes_slot *slot = vpp_native_find_bytes(handle);
    if (!slot || value < 0 || value > 255 || slot->length >= VPP_NATIVE_BYTES_MAX) return -1;
    if (vpp_native_bytes_reserve(slot, 1) != 0) return -1;
    slot->data[slot->length++] = (unsigned char)value;
    slot->data[slot->length] = 0;
    return 0;
}

// Binary-safe append; self-concatenation is allowed. Re-find the source after
// reserve because realloc can relocate a buffer (not its slot metadata).
int32_t vpp_native_bytes_concat(int32_t destination, int32_t source) {
    vpp_native_bytes_slot *dst = vpp_native_find_bytes(destination);
    vpp_native_bytes_slot *src = vpp_native_find_bytes(source);
    if (!dst || !src) return -1;
    size_t length = src->length;
    if (vpp_native_bytes_reserve(dst, length) != 0) return -1;
    memmove(dst->data + dst->length, src->data, length);
    dst->length += length;
    dst->data[dst->length] = 0;
    return 0;
}

// 1 = equal, 0 = different, -1 = invalid handle.
int32_t vpp_native_bytes_equal(int32_t left, int32_t right) {
    vpp_native_bytes_slot *a = vpp_native_find_bytes(left);
    vpp_native_bytes_slot *b = vpp_native_find_bytes(right);
    if (!a || !b) return -1;
    return a->length == b->length && memcmp(a->data, b->data, a->length) == 0;
}

// Create an independent, owned copy of a byte range. This is useful for
// lexing source text without requiring raw pointers in native Machine IR.
// Offsets and lengths count bytes (not Unicode code points).
int32_t vpp_native_bytes_slice(int32_t handle, int32_t start, int32_t count) {
    vpp_native_bytes_slot *src = vpp_native_find_bytes(handle);
    if (!src || start < 0 || count < 0 || (size_t)start > src->length ||
        (size_t)count > src->length - (size_t)start) return -1;
    int32_t result = vpp_native_bytes_new();
    if (result < 0) return -1;
    vpp_native_bytes_slot *dst = vpp_native_find_bytes(result);
    if (vpp_native_bytes_reserve(dst, (size_t)count) != 0) {
        free(dst->data);
        memset(dst, 0, sizeof(*dst));
        return -1;
    }
    memcpy(dst->data, src->data + (size_t)start, (size_t)count);
    dst->length = (size_t)count;
    dst->data[dst->length] = 0;
    return result;
}

// Find the first occurrence of a binary sequence, including embedded NUL.
// -1 = absent; -2 = invalid handle. Empty needle matches at offset zero.
int32_t vpp_native_bytes_find(int32_t haystack, int32_t needle) {
    vpp_native_bytes_slot *src = vpp_native_find_bytes(haystack);
    vpp_native_bytes_slot *target = vpp_native_find_bytes(needle);
    if (!src || !target) return -2;
    if (target->length == 0) return 0;
    if (target->length > src->length) return -1;
    size_t last = src->length - target->length;
    for (size_t i = 0; i <= last; ++i) {
        if (src->data[i] == target->data[0] &&
            memcmp(src->data + i, target->data, target->length) == 0) {
            return (int32_t)i;
        }
    }
    return -1;
}

// Reset a buffer for reuse without releasing its handle or capacity.
int32_t vpp_native_bytes_clear(int32_t handle) {
    vpp_native_bytes_slot *slot = vpp_native_find_bytes(handle);
    if (!slot) return -1;
    slot->length = 0;
    slot->data[0] = 0;
    return 0;
}

// Write UTF-8 or arbitrary binary bytes unchanged, without appending '\n'.
// Unlike in <i32>, this explicit API takes an owned bytes handle.
int32_t vpp_native_bytes_print(int32_t handle) {
    vpp_native_bytes_slot *slot = vpp_native_find_bytes(handle);
    if (!slot) return -1;
    size_t written = fwrite(slot->data, 1, slot->length, stdout);
    return written == slot->length ? (int32_t)written : -1;
}

int32_t vpp_native_bytes_length(int32_t handle) {
    vpp_native_bytes_slot *slot = vpp_native_find_bytes(handle);
    return slot ? (int32_t)slot->length : -1;
}

int32_t vpp_native_bytes_get(int32_t handle, int32_t index) {
    vpp_native_bytes_slot *slot = vpp_native_find_bytes(handle);
    if (!slot || index < 0 || (size_t)index >= slot->length) return -1;
    return slot->data[index];
}

// Store fixed-width signed integers in a portable little-endian wire format.
// Offset is measured in bytes; writes never grow an existing buffer.
int32_t vpp_native_bytes_push_i32(int32_t handle, int32_t value) {
    vpp_native_bytes_slot *slot = vpp_native_find_bytes(handle);
    if (!slot || vpp_native_bytes_reserve(slot, 4) != 0) return -1;
    uint32_t bits;
    memcpy(&bits, &value, sizeof(bits));
    for (size_t i = 0; i < 4; ++i) {
        slot->data[slot->length + i] = (unsigned char)(bits >> (8u * i));
    }
    slot->length += 4;
    slot->data[slot->length] = 0;
    return 0;
}

int32_t vpp_native_bytes_load_i32(int32_t handle, int32_t offset) {
    vpp_native_bytes_slot *slot = vpp_native_find_bytes(handle);
    if (!slot || offset < 0 || (size_t)offset > slot->length ||
        slot->length - (size_t)offset < 4) return -1;
    uint32_t bits = 0;
    for (size_t i = 0; i < 4; ++i) {
        bits |= (uint32_t)slot->data[(size_t)offset + i] << (8u * i);
    }
    int32_t value;
    memcpy(&value, &bits, sizeof(value));
    return value;
}

int32_t vpp_native_bytes_store_i32(int32_t handle, int32_t offset, int32_t value) {
    vpp_native_bytes_slot *slot = vpp_native_find_bytes(handle);
    if (!slot || offset < 0 || (size_t)offset > slot->length ||
        slot->length - (size_t)offset < 4) return -1;
    uint32_t bits;
    memcpy(&bits, &value, sizeof(bits));
    for (size_t i = 0; i < 4; ++i) {
        slot->data[(size_t)offset + i] = (unsigned char)(bits >> (8u * i));
    }
    return 0;
}

int32_t vpp_native_bytes_free(int32_t handle) {
    vpp_native_bytes_slot *slot = vpp_native_find_bytes(handle);
    if (!slot) return -1;
    free(slot->data);
    memset(slot, 0, sizeof(*slot));
    return 0;
}

static int32_t vpp_native_file_open(int32_t path_handle, const char *mode) {
    vpp_native_bytes_slot *path = vpp_native_find_bytes(path_handle);
    if (!path || path->length == 0 || path->length > VPP_NATIVE_PATH_MAX ||
        memchr(path->data, 0, path->length)) return -1;
    for (size_t i = 0; i < VPP_NATIVE_FILE_SLOTS; ++i) {
        vpp_native_file_slot *slot = &vpp_native_files[i];
        if (slot->id != 0) continue;
        // A full handle table must not truncate/create a file.
        if (vpp_native_next_handle == INT32_MAX) return -1;
        FILE *stream = fopen((const char *)path->data, mode);
        if (!stream) return -1;
        const int32_t id = vpp_native_allocate_handle();
        slot->id = id;
        slot->stream = stream;
        return id;
    }
    return -1;
}

int32_t vpp_native_file_open_read(int32_t path_handle) {
    return vpp_native_file_open(path_handle, "rb");
}

int32_t vpp_native_file_open_write(int32_t path_handle) {
    return vpp_native_file_open(path_handle, "wb");
}

// Byte reads: [0,255] = data, -1 = EOF, -2 = invalid handle or I/O error.
int32_t vpp_native_file_read_byte(int32_t handle) {
    vpp_native_file_slot *slot = vpp_native_find_file(handle);
    if (!slot) return -2;
    const int ch = fgetc(slot->stream);
    if (ch == EOF) return ferror(slot->stream) ? -2 : -1;
    return ch;
}

int32_t vpp_native_file_write_byte(int32_t handle, int32_t value) {
    vpp_native_file_slot *slot = vpp_native_find_file(handle);
    if (!slot || value < 0 || value > 255) return -1;
    return fputc(value, slot->stream) == EOF ? -1 : 0;
}

// Returns the number of bytes added, 0 at EOF, or -1 on error. Each call
// transfers at most 4096 bytes. The output buffer retains any prior content.
// Capacity is checked before reading, so a full buffer consumes no input.
int32_t vpp_native_file_read_block(int32_t file_handle, int32_t bytes_handle) {
    vpp_native_file_slot *file = vpp_native_find_file(file_handle);
    vpp_native_bytes_slot *bytes = vpp_native_find_bytes(bytes_handle);
    if (!file || !bytes || bytes->length == VPP_NATIVE_BYTES_MAX) return -1;
    size_t available = VPP_NATIVE_BYTES_MAX - bytes->length;
    size_t requested = available < VPP_NATIVE_IO_CHUNK ? available : VPP_NATIVE_IO_CHUNK;
    if (vpp_native_bytes_reserve(bytes, requested) != 0) return -1;
    size_t read_count = fread(bytes->data + bytes->length, 1, requested, file->stream);
    bytes->length += read_count;
    bytes->data[bytes->length] = 0;
    if (ferror(file->stream)) return -1;
    return (int32_t)read_count;
}

// A short write is reported as the actual count so the caller can detect it.
// For a stable buffer, file_write_block returns all bytes or a short count.
int32_t vpp_native_file_write_block(int32_t file_handle, int32_t bytes_handle) {
    vpp_native_file_slot *file = vpp_native_find_file(file_handle);
    vpp_native_bytes_slot *bytes = vpp_native_find_bytes(bytes_handle);
    if (!file || !bytes) return -1;
    return (int32_t)fwrite(bytes->data, 1, bytes->length, file->stream);
}

int32_t vpp_native_file_close(int32_t handle) {
    vpp_native_file_slot *slot = vpp_native_find_file(handle);
    if (!slot) return -1;
    int status = fclose(slot->stream);
    slot->stream = NULL;
    slot->id = 0;
    return status == 0 ? 0 : -1;
}

// Startup arguments are copied into owned byte handles; C pointers never
// cross the current i32 native ABI.
int32_t vpp_native_argc(void) {
#ifdef __APPLE__
    return *_NSGetArgc();
#elif defined(__linux__)
    FILE *file = fopen("/proc/self/cmdline", "rb");
    if (!file) return -1;
    int32_t count = 0;
    int ch, last = 0;
    while ((ch = fgetc(file)) != EOF) {
        if (ch == 0) ++count;
        last = ch;
    }
    if (ferror(file)) count = -1;
    if (count >= 0 && last != 0) ++count;
    if (fclose(file) != 0) return -1;
    return count;
#else
    return -1;
#endif
}

int32_t vpp_native_argv(int32_t index) {
#ifdef __APPLE__
    int argc = *_NSGetArgc();
    if (index < 0 || index >= argc) return -1;
    const char *value = (*_NSGetArgv())[index];
    if (!value) return -1;
    size_t length = strlen(value);
    if (length > VPP_NATIVE_BYTES_MAX) return -1;
    int32_t handle = vpp_native_bytes_new();
    if (handle < 0) return -1;
    vpp_native_bytes_slot *slot = vpp_native_find_bytes(handle);
    if (vpp_native_bytes_reserve(slot, length) != 0) {
        vpp_native_bytes_free(handle);
        return -1;
    }
    memcpy(slot->data, value, length + 1);
    slot->length = length;
    return handle;
#elif defined(__linux__)
    if (index < 0) return -1;
    FILE *file = fopen("/proc/self/cmdline", "rb");
    if (!file) return -1;
    int32_t handle = -1;
    int32_t current = 0;
    int ch;
    while ((ch = fgetc(file)) != EOF) {
        if (current == index) {
            if (handle < 0) {
                handle = vpp_native_bytes_new();
                if (handle < 0) break;
            }
            if (ch == 0) break;
            if (vpp_native_bytes_append(handle, ch) != 0) {
                vpp_native_bytes_free(handle);
                handle = -1;
                break;
            }
        }
        if (ch == 0) ++current;
    }
    if (ferror(file)) {
        if (handle >= 0) vpp_native_bytes_free(handle);
        handle = -1;
    }
    if (fclose(file) != 0) {
        if (handle >= 0) vpp_native_bytes_free(handle);
        return -1;
    }
    return handle;
#else
    (void)index;
    return -1;
#endif
}
#endif

// Opaque persistent byte records. Compilation policy and serialization live
// in V++; this boundary only hashes, validates and atomically publishes bytes.
// Compiler content is part of the namespace, so replacing a compiler never
// reuses artifacts produced by its previous implementation.
#if defined(__APPLE__) || defined(__linux__)
static unsigned long vpp_cache_hits, vpp_cache_misses, vpp_cache_writes;
static int vpp_cache_initialized;
static unsigned char vpp_cache_compiler[CC_SHA256_DIGEST_LENGTH];

static void vpp_native_cache_report(void) {
    const char *stats = getenv("VPP_NATIVE_CACHE_STATS");
    if (stats && strcmp(stats, "1") == 0)
        fprintf(stderr, "native cache: hits=%lu misses=%lu writes=%lu\n",
                vpp_cache_hits, vpp_cache_misses, vpp_cache_writes);
}

static int vpp_native_cache_path(const vpp_native_bytes_slot *key,
                                 char *path, size_t capacity) {
    const char *disabled = getenv("VPP_NATIVE_NO_CACHE");
    if (disabled && strcmp(disabled, "1") == 0) return -1;
    if (!vpp_cache_initialized) {
        // Resolve the running image instead of trusting argv[0] or PATH.
        char executable[VPP_NATIVE_PATH_MAX + 1];
#ifdef __APPLE__
        uint32_t size = sizeof executable;
        if (_NSGetExecutablePath(executable, &size) != 0) return -1;
#else
        ssize_t size = readlink("/proc/self/exe", executable, sizeof executable - 1);
        if (size <= 0 || (size_t)size >= sizeof executable) return -1;
        executable[size] = '\0';
#endif
        FILE *file = fopen(executable, "rb");
        if (!file) return -1;
        CC_SHA256_CTX digest;
        CC_SHA256_Init(&digest);
        unsigned char block[16384];
        size_t count;
        while ((count = fread(block, 1, sizeof block, file)) != 0)
            CC_SHA256_Update(&digest, block, (CC_LONG)count);
        int failed = ferror(file);
        if (fclose(file) != 0) failed = 1;
        if (failed) return -1;
        CC_SHA256_Final(vpp_cache_compiler, &digest);
        vpp_cache_initialized = 1;
        atexit(vpp_native_cache_report);
    }
    const char *directory = getenv("VPP_NATIVE_CACHE_DIR");
    if (!directory || !*directory) directory = ".vpp-native-cache";
    if (strlen(directory) > VPP_NATIVE_PATH_MAX - 80) return -1;
    if (mkdir(directory, 0700) != 0 && errno != EEXIST) return -1;
    struct stat info;
    if (lstat(directory, &info) != 0 || !S_ISDIR(info.st_mode)) return -1;
    unsigned char hash[CC_SHA256_DIGEST_LENGTH];
    CC_SHA256_CTX digest;
    CC_SHA256_Init(&digest);
    CC_SHA256_Update(&digest, vpp_cache_compiler, sizeof vpp_cache_compiler);
    CC_SHA256_Update(&digest, key->data, (CC_LONG)key->length);
    CC_SHA256_Final(hash, &digest);
    char hex[CC_SHA256_DIGEST_LENGTH * 2 + 1];
#ifdef VPP_NATIVE_VPP_RUNTIME
    // The native boundary computes SHA-256; V++ owns its byte-to-hex
    // representation. All scratch addresses are offsets into the 64-bit arena.
    unsigned char *arena = vpp_native_memory_base();
    if (!arena) return -1;
    memcpy(arena + 3216, hash, sizeof hash);
    if (vpp_native_cache_digest_hex() != 0) return -1;
    memcpy(hex, arena + 3248, sizeof hex);
#else
    for (size_t i = 0; i < sizeof hash; ++i)
        snprintf(hex + i * 2, 3, "%02x", hash[i]);
#endif
    int length = snprintf(path, capacity, "%s/%s.cache", directory, hex);
    return length >= 0 && (size_t)length < capacity ? 0 : -1;
}
#endif

// 1 = hit, 0 = miss/disabled/unavailable, -1 = invalid handles. A miss leaves
// the destination empty. Verify the exact key and payload digest, including
// lengths, before exposing any cached data to the compiler.
int32_t vpp_native_cache_read(int32_t key_handle, int32_t data_handle) {
    vpp_native_bytes_slot *key = vpp_native_find_bytes(key_handle);
    vpp_native_bytes_slot *data = vpp_native_find_bytes(data_handle);
    if (!key || !data || key == data || key->length == 0) return -1;
    vpp_native_bytes_clear(data_handle);
#if defined(__APPLE__) || defined(__linux__)
    char path[VPP_NATIVE_PATH_MAX + 1];
    if (vpp_native_cache_path(key, path, sizeof path) != 0) return 0;
    ++vpp_cache_misses;
    int fd = open(path, O_RDONLY | O_NOFOLLOW | O_NONBLOCK);
    if (fd < 0) return 0;
    struct stat info;
    if (fstat(fd, &info) != 0 || !S_ISREG(info.st_mode) ||
        info.st_size < 48 || info.st_size > 48 + 2 * VPP_NATIVE_BYTES_MAX) {
        close(fd); return 0;
    }
    FILE *file = fdopen(fd, "rb");
    if (!file) { close(fd); return 0; }
    unsigned char header[48], hash[CC_SHA256_DIGEST_LENGTH];
    unsigned char *saved_key = NULL;
    int hit = 0;
    if (fread(header, 1, sizeof header, file) != sizeof header)
        goto cache_read_done;
    uint32_t key_size = 0, data_size = 0;
#ifdef VPP_NATIVE_VPP_RUNTIME
    // The V++ runtime validates cache framing in the bounded arena. C keeps
    // the OS trust boundary (fstat, O_NOFOLLOW, checksum and I/O).
    unsigned char *arena = vpp_native_memory_base();
    if (!arena || info.st_size > INT32_MAX) goto cache_read_done;
    memcpy(arena + 3200, header, 16);
    int32_t decoded_size = vpp_native_cache_header_check(key_handle, (int32_t)info.st_size);
    if (decoded_size < 0) goto cache_read_done;
    key_size = (uint32_t)key->length;
    data_size = (uint32_t)decoded_size;
#else
    if (memcmp(header, "VPPCACH1", 8) != 0) goto cache_read_done;
    for (unsigned i = 0; i < 4; ++i) {
        key_size |= (uint32_t)header[8 + i] << (8u * i);
        data_size |= (uint32_t)header[12 + i] << (8u * i);
    }
#endif
    if (key_size != key->length || data_size > VPP_NATIVE_BYTES_MAX ||
        (uint64_t)info.st_size != 48u + (uint64_t)key_size + data_size)
        goto cache_read_done;
    saved_key = malloc(key_size);
    if (!saved_key || fread(saved_key, 1, key_size, file) != key_size ||
        memcmp(saved_key, key->data, key_size) != 0 ||
        vpp_native_bytes_reserve(data, data_size) != 0 ||
        fread(data->data, 1, data_size, file) != data_size ||
        fgetc(file) != EOF || ferror(file)) goto cache_read_done;
    CC_SHA256_CTX digest;
    CC_SHA256_Init(&digest);
    CC_SHA256_Update(&digest, header, 16);
    CC_SHA256_Update(&digest, saved_key, key_size);
    CC_SHA256_Update(&digest, data->data, data_size);
    CC_SHA256_Final(hash, &digest);
    if (memcmp(hash, header + 16, sizeof hash) != 0) goto cache_read_done;
    data->length = data_size;
    data->data[data_size] = 0;
    hit = 1;
cache_read_done:
    free(saved_key);
    if (fclose(file) != 0) hit = 0;
    if (hit) { ++vpp_cache_hits; --vpp_cache_misses; }
    else vpp_native_bytes_clear(data_handle);
    return hit;
#else
    return 0;
#endif
}

// Cache failure is advisory; callers continue with a normal build. Readers
// see either the complete old record or the complete new one during races.
int32_t vpp_native_cache_write(int32_t key_handle, int32_t data_handle) {
    vpp_native_bytes_slot *key = vpp_native_find_bytes(key_handle);
    vpp_native_bytes_slot *data = vpp_native_find_bytes(data_handle);
    if (!key || !data || key == data || key->length == 0) return -1;
#if defined(__APPLE__) || defined(__linux__)
    unsigned char header[48] = "VPPCACH1";
#ifdef VPP_NATIVE_VPP_RUNTIME
    if (vpp_native_cache_header_write(key_handle, data_handle) != 0) return -1;
    unsigned char *arena = vpp_native_memory_base();
    if (!arena) return -1;
    memcpy(header, arena + 3200, 16);
#else
    for (unsigned i = 0; i < 4; ++i) {
        header[8 + i] = (unsigned char)(key->length >> (8u * i));
        header[12 + i] = (unsigned char)(data->length >> (8u * i));
    }
#endif
    char path[VPP_NATIVE_PATH_MAX + 1], temporary[VPP_NATIVE_PATH_MAX + 16];
    if (vpp_native_cache_path(key, path, sizeof path) != 0) return -1;
    int length = snprintf(temporary, sizeof temporary, "%s.XXXXXX", path);
    if (length < 0 || (size_t)length >= sizeof temporary) return -1;
    int fd = mkstemp(temporary);
    if (fd < 0) return -1;
    FILE *file = fdopen(fd, "wb");
    if (!file) { close(fd); unlink(temporary); return -1; }
    CC_SHA256_CTX digest;
    CC_SHA256_Init(&digest);
    CC_SHA256_Update(&digest, header, 16);
    CC_SHA256_Update(&digest, key->data, (CC_LONG)key->length);
    CC_SHA256_Update(&digest, data->data, (CC_LONG)data->length);
    CC_SHA256_Final(header + 16, &digest);
    int failed = fwrite(header, 1, sizeof header, file) != sizeof header ||
        fwrite(key->data, 1, key->length, file) != key->length ||
        fwrite(data->data, 1, data->length, file) != data->length;
    if (fclose(file) != 0) failed = 1;
    if (!failed && rename(temporary, path) == 0) {
        ++vpp_cache_writes;
        return 0;
    }
    unlink(temporary);
#endif
    return -1;
}

// Link a V++-generated Mach-O object without passing arguments through a
// shell. Only a successful link replaces the destination.
// Locate the native runtime from VPP_HOME or a workspace ancestor. Never
// invoke the compiler through a shell or accept an arbitrary relative path.
static int vpp_native_runtime_file(const char *relative, char *target, size_t capacity) {
#if defined(__APPLE__) || defined(__linux__)
#ifdef VPP_NATIVE_VPP_RUNTIME
    // V++ resolves only the trusted runtime paths. C retains the final OS
    // regular-file check before passing a discovered path to clang.
    int32_t kind = -1;
    if (strcmp(relative, "scripts/ASM/vpp_aot_primitives_aarch64.S") == 0)
        kind = 1;
    else if (strcmp(relative, "src/runtime/native/vpp_aot_runtime.c") == 0)
        kind = 2;
    if (kind < 0) return -1;
    int32_t handle = vpp_native_runtime_file_path(kind);
    if (handle <= 0) return -1;
    int result = -1;
    vpp_native_bytes_slot *path = vpp_native_find_bytes(handle);
    struct stat info;
    if (path && path->length > 0 && path->length < capacity &&
        !memchr(path->data, 0, path->length) &&
        path->data[path->length] == 0 &&
        stat((const char *)path->data, &info) == 0 &&
        S_ISREG(info.st_mode)) {
        memcpy(target, path->data, path->length + 1);
        result = 0;
    }
    vpp_native_bytes_free(handle);
    return result;
#else
    const char *home = getenv("VPP_HOME");
    struct stat info;
    if (home && home[0] == '/') {
        int length = snprintf(target, capacity, "%s/%s", home, relative);
        if (length > 0 && (size_t)length < capacity &&
            stat(target, &info) == 0 && S_ISREG(info.st_mode)) return 0;
    }
    char directory[VPP_NATIVE_PATH_MAX];
    if (!getcwd(directory, sizeof directory)) return -1;
    for (;;) {
        int length = snprintf(target, capacity, "%s/%s", directory, relative);
        if (length > 0 && (size_t)length < capacity &&
            stat(target, &info) == 0 && S_ISREG(info.st_mode)) return 0;
        char *slash = strrchr(directory, '/');
        if (!slash) break;
        if (slash == directory) {
            if (directory[1] == 0) break;
            directory[1] = 0;
        } else {
            *slash = 0;
        }
    }
#endif
#else
    (void)relative; (void)target; (void)capacity;
#endif
    return -1;
}

static int vpp_native_runtime_source(char *target, size_t capacity) {
    return vpp_native_runtime_file("src/runtime/native/vpp_aot_runtime.c", target, capacity);
}

#ifndef VPP_NATIVE_VPP_RUNTIME
int32_t vpp_native_runtime_module_path(void) {
    char path[VPP_NATIVE_PATH_MAX + 1];
    // The C/POSIX runtime supplies every native ABI symbol on both Linux and
    // bootstrap macOS. Loading runtime.vi here would duplicate the runtime
    // and introduce unresolved máy_* assembly primitive references.
    const char *module = "gói/biên dịch/native/runtime_c.vi";
    if (vpp_native_runtime_file(module, path, sizeof path) != 0)
        return -1;
    const size_t length = strlen(path);
    int32_t handle = vpp_native_bytes_new();
    vpp_native_bytes_slot *bytes = vpp_native_find_bytes(handle);
    if (!bytes || vpp_native_bytes_reserve(bytes, length) != 0) {
        if (handle > 0) vpp_native_bytes_free(handle);
        return -1;
    }
    memcpy(bytes->data, path, length + 1);
    bytes->length = length;
    return handle;
}
#endif

int32_t vpp_native_link_macho(int32_t object_handle, int32_t output_handle,
                              int32_t source_handle) {
#if defined(__APPLE__) && defined(VPP_NATIVE_VPP_RUNTIME)
    int32_t archive = vpp_native_link_archive_single(object_handle);
    if (archive <= 0) return -1;
    int32_t result = vpp_native_link_macho_modules(archive, output_handle, source_handle);
    vpp_native_bytes_free(archive);
    return result;
#elif defined(__APPLE__)
    vpp_native_bytes_slot *object = vpp_native_find_bytes(object_handle);
    vpp_native_bytes_slot *output = vpp_native_find_bytes(output_handle);
    vpp_native_bytes_slot *source = vpp_native_find_bytes(source_handle);
    if (!object || !output || !source || object->length == 0 ||
        output->length == 0 || output->length > VPP_NATIVE_PATH_MAX - 32 ||
        source->length == 0 || source->length > VPP_NATIVE_PATH_MAX ||
        memchr(output->data, 0, output->length) ||
        memchr(source->data, 0, source->length)) return -1;

    const char *out = (const char *)output->data;
    struct stat src_info, dst_info;
    if (stat((const char *)source->data, &src_info) != 0 ||
        !S_ISREG(src_info.st_mode)) return -1;
    if (lstat(out, &dst_info) == 0) {
        if (!S_ISREG(dst_info.st_mode) ||
            (src_info.st_dev == dst_info.st_dev &&
             src_info.st_ino == dst_info.st_ino)) return -1;
    } else if (errno != ENOENT) return -1;

    char obj_path[VPP_NATIVE_PATH_MAX + 64];
    char exe_path[VPP_NATIVE_PATH_MAX + 64];
    const char *prefix = out[0] == '-' ? "./" : "";
    int obj_length = snprintf(obj_path, sizeof obj_path,
                              "%s%s.vpp-obj-XXXXXX", prefix, out);
    int exe_length = snprintf(exe_path, sizeof exe_path,
                              "%s%s.vpp-exe-XXXXXX", prefix, out);
    if (obj_length < 0 || (size_t)obj_length >= sizeof obj_path ||
        exe_length < 0 || (size_t)exe_length >= sizeof exe_path) return -1;
    int fd = mkstemp(obj_path);
    if (fd < 0) return -1;
    int exe_fd = -1;
    int result = -1;
    size_t done = 0;
    while (done < object->length) {
        ssize_t n = write(fd, object->data + done, object->length - done);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) goto cleanup;
        done += (size_t)n;
    }
    if (close(fd) != 0) { fd = -1; goto cleanup; }
    fd = -1;
    exe_fd = mkstemp(exe_path);
    if (exe_fd < 0) goto cleanup;
    if (close(exe_fd) != 0) { exe_fd = -1; goto cleanup; }
    exe_fd = -1;

    // section_64.nreloc is at offset 164 in our single-section MH_OBJECT.
    // The runtime is only linked for actual external CALL relocations.
    const int needs_runtime = object->length >= 168 &&
        (object->data[164] || object->data[165] ||
         object->data[166] || object->data[167]);
    char runtime_path[VPP_NATIVE_PATH_MAX + 64];
    if (needs_runtime &&
        vpp_native_runtime_source(runtime_path, sizeof runtime_path) != 0)
        goto cleanup;
    char *const argv[] = {"/usr/bin/clang", "-arch", "arm64",
                          obj_path, needs_runtime ? runtime_path : "-o",
                          needs_runtime ? "-o" : exe_path,
                          needs_runtime ? exe_path : NULL, NULL};
    extern char **environ;
    pid_t child;
    if (posix_spawn(&child, argv[0], NULL, NULL, argv, environ) != 0) goto cleanup;
    int status = 0;
    pid_t waited;
    do { waited = waitpid(child, &status, 0); }
    while (waited < 0 && errno == EINTR);
    if (waited != child || !WIFEXITED(status) || WEXITSTATUS(status) != 0)
        goto cleanup;
    if (stat(exe_path, &dst_info) != 0 || !S_ISREG(dst_info.st_mode) ||
        access(exe_path, X_OK) != 0) goto cleanup;
    if (rename(exe_path, out) != 0) goto cleanup;
    result = 0;

cleanup:
    if (fd >= 0) close(fd);
    if (exe_fd >= 0) close(exe_fd);
    unlink(obj_path);
    unlink(exe_path);
    return result;
#else
    (void)object_handle; (void)output_handle; (void)source_handle;
    return -1;
#endif
}

// Link a packed sequence of distinct MH_OBJECT files created by the V++
// module backend. The archive wire format is [u32 length][object bytes]...
// Each object gets its own temporary file, allowing the system Mach-O linker
// to resolve external BRANCH26 relocations between V++ modules.
int32_t vpp_native_link_macho_modules(int32_t archive_handle,
                                      int32_t output_handle,
                                      int32_t source_handle) {
#if defined(__APPLE__) || defined(__linux__)
    vpp_native_bytes_slot *archive = vpp_native_find_bytes(archive_handle);
    vpp_native_bytes_slot *output = vpp_native_find_bytes(output_handle);
    vpp_native_bytes_slot *source = vpp_native_find_bytes(source_handle);
    if (!archive || !output || !source || archive->length < 5) return -1;
#ifdef VPP_NATIVE_VPP_RUNTIME
    if (vpp_native_link_validate_files(output_handle, source_handle) != 0)
        return -1;
#else
    if (output->length == 0 || output->length > VPP_NATIVE_PATH_MAX - 48 ||
        source->length == 0 || source->length > VPP_NATIVE_PATH_MAX ||
        memchr(output->data, 0, output->length) ||
        memchr(source->data, 0, source->length)) return -1;
#endif

#ifndef VPP_NATIVE_VPP_RUNTIME
    const char *out = (const char *)output->data;
    struct stat src_info, dst_info;
    if (stat((const char *)source->data, &src_info) != 0 ||
        !S_ISREG(src_info.st_mode)) return -1;
    if (lstat(out, &dst_info) == 0) {
        if (!S_ISREG(dst_info.st_mode) ||
            (src_info.st_dev == dst_info.st_dev &&
             src_info.st_ino == dst_info.st_ino)) return -1;
    } else if (errno != ENOENT) return -1;
#endif

    // Count and validate the packed objects before allocating the link argv.
    // The old fixed-size array capped programs at 32 nonempty modules.
    size_t position = 0;
    size_t module_count = 0;
#ifdef VPP_NATIVE_VPP_RUNTIME
    // The V++ runtime owns archive framing, ARM64 Mach-O validation and
    // undefined compiler-service detection. C only performs OS/link I/O.
    int32_t summary = vpp_native_object_archive_summary(archive_handle);
    if (summary < 2) return -1;
    module_count = (size_t)(summary / 2);
    int needs_services = summary & 1;
#else
    while (position < archive->length) {
        if (archive->length - position < 4) return -1;
        uint32_t length = 0;
        for (unsigned i = 0; i < 4; ++i)
            length |= (uint32_t)archive->data[position + i] << (8u * i);
        position += 4;
        if (length < 64 || length > archive->length - position) return -1;
        const unsigned char *data = archive->data + position;
#ifdef __linux__
        // ELF64 little-endian ET_REL for EM_X86_64.
        if (data[0] != 0x7f || data[1] != 'E' ||
            data[2] != 'L' || data[3] != 'F' ||
            data[4] != 2 || data[5] != 1 || data[6] != 1 ||
            data[16] != 1 || data[17] != 0 ||
            data[18] != 62 || data[19] != 0) return -1;
#else
        // MH_MAGIC_64, CPU_TYPE_ARM64 and MH_OBJECT, all little-endian.
        if (data[0] != 0xcf || data[1] != 0xfa ||
            data[2] != 0xed || data[3] != 0xfe ||
            data[4] != 0x0c || data[5] != 0x00 ||
            data[6] != 0x00 || data[7] != 0x01 ||
            data[12] != 0x01 || data[13] != 0x00 ||
            data[14] != 0x00 || data[15] != 0x00) return -1;
#endif
        ++module_count;
        position += length;
    }
#endif
    if (module_count == 0 || module_count > (SIZE_MAX / sizeof(char *)) - 12)
        return -1;

    char (*object_paths)[VPP_NATIVE_PATH_MAX + 64] =
        calloc(module_count, sizeof(*object_paths));
    char **argv = calloc(module_count + 12, sizeof(*argv));
    char executable_path[VPP_NATIVE_PATH_MAX + 64];
    char runtime_path[VPP_NATIVE_PATH_MAX + 64];
#ifdef VPP_NATIVE_VPP_RUNTIME
    char primitive_path[VPP_NATIVE_PATH_MAX + 64];
    int32_t temporary_handle = -1;
#endif
#ifndef VPP_NATIVE_VPP_RUNTIME
    const char *prefix = out[0] == '-' ? "./" : "";
#endif
    size_t created = 0;
    int exe_fd = -1;
    int result = -1;
    int exe_created = 0;
    size_t argc = 0;
    if (!object_paths || !argv) goto cleanup_modules;
#ifdef VPP_NATIVE_VPP_RUNTIME
    temporary_handle = vpp_native_bytes_new();
    if (temporary_handle <= 0) goto cleanup_modules;
#endif
#ifdef __linux__
    const char *configured_cc = getenv("VPP_NATIVE_CC");
    argv[argc++] = (char *)(configured_cc && *configured_cc ? configured_cc : "cc");
#else
    argv[argc++] = "/usr/bin/clang";
    argv[argc++] = "-arch";
    argv[argc++] = "arm64";
#endif
    position = 0;
    while (position < archive->length) {
#ifdef VPP_NATIVE_VPP_RUNTIME
        // This was validated in the V++ summary pass above. Use the same
        // V++ decoder for exact object boundaries when publishing temp files.
        int32_t decoded = vpp_native_object_archive_entry(archive_handle,
                                                           (int32_t)position);
        if (decoded < 0) goto cleanup_modules;
        uint32_t length = (uint32_t)decoded;
#else
        uint32_t length = 0;
        for (unsigned i = 0; i < 4; ++i)
            length |= (uint32_t)archive->data[position + i] << (8u * i);
#endif
        position += 4;
#ifndef VPP_NATIVE_VPP_RUNTIME
        const unsigned char *data = archive->data + position;
#endif
#ifdef VPP_NATIVE_VPP_RUNTIME
        if (vpp_native_link_temporary_path(output_handle, temporary_handle,
                                           (int32_t)created) != 0)
            goto cleanup_modules;
        vpp_native_bytes_slot *temporary_bytes = vpp_native_find_bytes(temporary_handle);
        if (!temporary_bytes || temporary_bytes->length >= sizeof object_paths[created])
            goto cleanup_modules;
        memcpy(object_paths[created], temporary_bytes->data,
               temporary_bytes->length + 1);
#else
        int size = snprintf(object_paths[created], sizeof object_paths[created],
                            "%s%s.vpp-module-%zu-XXXXXX", prefix, out, created);
        if (size < 0 || (size_t)size >= sizeof object_paths[created])
            goto cleanup_modules;
#endif
        int fd;
#ifdef VPP_NATIVE_VPP_RUNTIME
        fd = vpp_native_link_temp_open(temporary_handle);
        if (fd >= 0) {
            // mkstemp has replaced XXXXXX in the same V++ bytes handle.
            memcpy(object_paths[created], temporary_bytes->data,
                   temporary_bytes->length + 1);
        }
#else
        fd = mkstemp(object_paths[created]);
#endif
        if (fd < 0) goto cleanup_modules;
        ++created;
#ifdef VPP_NATIVE_VPP_RUNTIME
        if (vpp_native_link_write_object(fd, archive_handle,
                                         (int32_t)(position - 4)) != (int32_t)length) {
            close(fd);
            goto cleanup_modules;
        }
#else
        size_t written = 0;
        while (written < length) {
            ssize_t n = write(fd, data + written, length - written);
            if (n < 0 && errno == EINTR) continue;
            if (n <= 0) { close(fd); goto cleanup_modules; }
            written += (size_t)n;
        }
#endif
        if (close(fd) != 0) goto cleanup_modules;
        argv[argc++] = object_paths[created - 1];
        position += length;
    }
#ifdef VPP_NATIVE_VPP_RUNTIME
    if (vpp_native_runtime_file("scripts/ASM/vpp_aot_primitives_aarch64.S",
                                primitive_path, sizeof primitive_path) != 0)
        goto cleanup_modules;
    if (needs_services && vpp_native_runtime_source(runtime_path, sizeof runtime_path) != 0)
        goto cleanup_modules;
#else
    if (vpp_native_runtime_source(runtime_path, sizeof runtime_path) != 0)
        goto cleanup_modules;
#endif
#ifdef VPP_NATIVE_VPP_RUNTIME
    if (vpp_native_link_temporary_path(output_handle, temporary_handle, -1) != 0)
        goto cleanup_modules;
    vpp_native_bytes_slot *temporary_bytes = vpp_native_find_bytes(temporary_handle);
    if (!temporary_bytes || temporary_bytes->length >= sizeof executable_path)
        goto cleanup_modules;
    memcpy(executable_path, temporary_bytes->data, temporary_bytes->length + 1);
#else
    int path_len = snprintf(executable_path, sizeof executable_path,
                            "%s%s.vpp-exe-XXXXXX", prefix, out);
    if (path_len < 0 || (size_t)path_len >= sizeof executable_path)
        goto cleanup_modules;
#endif
#ifdef VPP_NATIVE_VPP_RUNTIME
    exe_fd = vpp_native_link_temp_open(temporary_handle);
    if (exe_fd >= 0)
        memcpy(executable_path, temporary_bytes->data, temporary_bytes->length + 1);
#else
    exe_fd = mkstemp(executable_path);
#endif
    if (exe_fd < 0) goto cleanup_modules;
    exe_created = 1;
    if (close(exe_fd) != 0) { exe_fd = -1; goto cleanup_modules; }
    exe_fd = -1;
#ifdef VPP_NATIVE_VPP_RUNTIME
    argv[argc++] = "-DVPP_NATIVE_VPP_RUNTIME=1";
    argv[argc++] = "-DVPP_NATIVE_RUNTIME_STACK_ABI=1";
    argv[argc++] = primitive_path;
    if (needs_services) argv[argc++] = runtime_path;
#else
    argv[argc++] = runtime_path;
#ifdef __linux__
    argv[argc++] = "-lcrypto";
#endif
#endif
    argv[argc++] = "-o";
    argv[argc++] = executable_path;
    argv[argc] = NULL;
    extern char **environ;
#ifdef VPP_NATIVE_VPP_RUNTIME
    // The C linker owns argv storage. Register the two full-width pointers
    // only for the duration of V++'s synchronous spawn/wait operation.
    unsigned char *arena = vpp_native_memory_base();
    if (!arena || sizeof(argv) != 8 || sizeof(environ) != 8)
        goto cleanup_modules;
    memcpy(arena + 3344, &argv, sizeof(argv));
    memcpy(arena + 3352, &environ, sizeof(environ));
    if (vpp_native_link_spawn_wait() != 0) goto cleanup_modules;
#else
    pid_t child;
    if (posix_spawnp(&child, argv[0], NULL, NULL, argv, environ) != 0)
        goto cleanup_modules;
    int child_status = 0;
    pid_t waited;
    do { waited = waitpid(child, &child_status, 0); }
    while (waited < 0 && errno == EINTR);
    if (waited != child || !WIFEXITED(child_status) ||
        WEXITSTATUS(child_status) != 0) goto cleanup_modules;
#endif
#ifdef VPP_NATIVE_VPP_RUNTIME
    if (vpp_native_link_executable_ready(temporary_handle) != 0)
        goto cleanup_modules;
    if (vpp_native_link_publish(temporary_handle, output_handle) != 0)
        goto cleanup_modules;
#else
    if (stat(executable_path, &dst_info) != 0 ||
        !S_ISREG(dst_info.st_mode) || access(executable_path, X_OK) != 0)
        goto cleanup_modules;
    if (rename(executable_path, out) != 0) goto cleanup_modules;
#endif
    result = 0;

cleanup_modules:
#ifdef VPP_NATIVE_VPP_RUNTIME
    if (temporary_handle > 0) vpp_native_bytes_free(temporary_handle);
#endif
    if (exe_fd >= 0) close(exe_fd);
    if (exe_created) unlink(executable_path);
    for (size_t i = 0; i < created; ++i) unlink(object_paths[i]);
    free(argv);
    free(object_paths);
    return result;
#else
    (void)archive_handle; (void)output_handle; (void)source_handle;
    return -1;
#endif
}

#endif // !VPP_NATIVE_VPP_RUNTIME: legacy C fallback only
