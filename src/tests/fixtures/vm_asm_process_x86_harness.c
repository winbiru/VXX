// Tests the POSIX ASM process boundary on native ARM64 and x86-64 (including
// Rosetta). This C file is a test driver only; the runtime remains V++/ASM.
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/time.h>

enum { ARENA_SIZE = 68162560, CONTROL = 4096, DATA = 8389000 };
static unsigned char *arena;
extern int32_t vpp_native_os_process(int32_t operation, int32_t control);

unsigned char *vpp_native_memory_base(void) {
    if (!arena) {
        arena = mmap(NULL, ARENA_SIZE, PROT_READ | PROT_WRITE,
                     MAP_PRIVATE | MAP_ANON, -1, 0);
        if (arena == MAP_FAILED) arena = NULL;
    }
    return arena;
}

static void put32(unsigned offset, int32_t value) {
    memcpy(arena + CONTROL + offset, &value, sizeof value);
}
static int32_t get32(unsigned offset) {
    int32_t result;
    memcpy(&result, arena + CONTROL + offset, sizeof result);
    return result;
}
static void set_offset(unsigned pos, uint32_t value) {
    memcpy(arena + pos, &value, sizeof value);
}
static unsigned append_string(unsigned *next, const char *text) {
    size_t n = strlen(text) + 1;
    assert(n <= 4096 && *next + n < ARENA_SIZE);
    unsigned offset = *next;
    memcpy(arena + offset, text, n);
    *next += (unsigned)n;
    return offset;
}
static int spawn_process(const char *const *argv, const char *const *env) {
    memset(arena + CONTROL, 0, 1024);
    unsigned next = DATA;
    int argc = 0;
    while (argv[argc]) ++argc;
    assert(argc > 0 && argc < 128);
    for (int i = 0; i < argc; ++i)
        set_offset(8500000 + (unsigned)i * 4, append_string(&next, argv[i]));
    put32(12, 8500000);
    put32(16, 8501000);
    put32(20, argc);
    put32(4, -1);
    put32(8, -1);
    if (env && env[0]) {
        int count = 0;
        while (env[count]) {
            assert(count < 128);
            set_offset(8503000 + (unsigned)count * 4,
                       append_string(&next, env[count]));
            ++count;
        }
        put32(24, 8503000);
        put32(44, count);
        put32(56, 8550000);
    }
    return vpp_native_os_process(0, CONTROL);
}

static void drain(char *out, size_t out_cap, char *err, size_t err_cap) {
    size_t out_n = 0, err_n = 0;
    for (int round = 0; round < 100; ++round) {
        put32(28, 2000);
        int ready = vpp_native_os_process(1, CONTROL);
        assert(ready >= 0);
        for (int channel = 1; channel <= 2; ++channel) {
            if (!(ready & (1 << (channel - 1)))) continue;
            put32(32, 8400000);
            put32(36, 4096);
            put32(40, channel);
            int n = vpp_native_os_process(2, CONTROL);
            assert(n >= 0);
            char *dest = channel == 1 ? out : err;
            size_t *used = channel == 1 ? &out_n : &err_n;
            size_t limit = channel == 1 ? out_cap : err_cap;
            assert(*used + (size_t)n < limit);
            memcpy(dest + *used, arena + 8400000, (size_t)n);
            *used += (size_t)n;
            dest[*used] = 0;
        }
        if (ready == 4) break;
        assert(round < 99);
    }
    assert(get32(4) == -1 && get32(8) == -1);
}

static int open_fd_count(void) {
    int count = 0;
    for (int fd = 0; fd < 256; ++fd) {
        errno = 0;
        if (fcntl(fd, F_GETFD) >= 0 || errno != EBADF) ++count;
    }
    return count;
}

static void poll_signal_handler(int signal_number) {
    (void)signal_number;
}

int main(void) {
    assert(vpp_native_memory_base() != NULL);
    assert(vpp_native_os_process(4, -1) == -22);
    assert(vpp_native_os_process(4, 67000000) == -22);
    int original_fds = open_fd_count();
    const char *const first[] = {
        "/bin/sh", "-c", "printf hello; printf oops >&2; exit 7", NULL
    };
    char out[65536] = {0}, err[65536] = {0};
    assert(spawn_process(first, NULL) == 1);
    assert(get32(0) > 0 && get32(4) >= 0 && get32(8) >= 0);
    drain(out, sizeof out, err, sizeof err);
    assert(!strcmp(out, "hello") && !strcmp(err, "oops"));
    assert(vpp_native_os_process(3, CONTROL) == 7);
    assert(vpp_native_os_process(4, CONTROL) == 0);

    const char *const unicode[] = {"/bin/echo", "A B", "á🐱", NULL};
    memset(out, 0, sizeof out);
    memset(err, 0, sizeof err);
    assert(spawn_process(unicode, NULL) == 1);
    drain(out, sizeof out, err, sizeof err);
    assert(!strcmp(out, "A B á🐱\n"));
    assert(vpp_native_os_process(3, CONTROL) == 0);
    assert(vpp_native_os_process(4, CONTROL) == 0);

    const char *const env[] = {"VPP_VM_PROCESS_X86=mới", "PATH=vpp-only-child", NULL};
    const char *const envcheck[] = {
        "/bin/sh", "-c", "printf '%s|%s' \"$VPP_VM_PROCESS_X86\" \"$PATH\"", NULL
    };
    memset(out, 0, sizeof out);
    memset(err, 0, sizeof err);
    assert(setenv("VPP_VM_PROCESS_X86", "parent", 1) == 0);
    assert(spawn_process(envcheck, env) == 1);
    drain(out, sizeof out, err, sizeof err);
    assert(!strcmp(out, "mới|vpp-only-child"));
    assert(!strcmp(getenv("VPP_VM_PROCESS_X86"), "parent"));
    assert(vpp_native_os_process(3, CONTROL) == 0);
    assert(vpp_native_os_process(4, CONTROL) == 0);

    const char *const signal_child[] = {"/bin/sh", "-c", "kill -TERM $$", NULL};
    assert(spawn_process(signal_child, NULL) == 1);
    memset(out, 0, sizeof out);
    memset(err, 0, sizeof err);
    drain(out, sizeof out, err, sizeof err);
    assert(vpp_native_os_process(3, CONTROL) == 143);
    assert(vpp_native_os_process(4, CONTROL) == 0);

    const char *const missing[] = {"/this-vpp-program-must-not-exist", NULL};
    int missing_status = spawn_process(missing, NULL);
    if (missing_status == 0) {
        assert(get32(48) == ENOENT);
    } else {
        // POSIX also permits failure after the successful parent return.
        assert(missing_status == 1);
        drain(out, sizeof out, err, sizeof err);
        assert(vpp_native_os_process(3, CONTROL) == 127);
    }
    assert(vpp_native_os_process(4, CONTROL) == 0);

    const char *const sleeper[] = {"/bin/sleep", "5", NULL};
    assert(spawn_process(sleeper, NULL) == 1);
    struct sigaction action = {0}, previous = {0};
    action.sa_handler = poll_signal_handler;
    sigemptyset(&action.sa_mask);
    assert(sigaction(SIGALRM, &action, &previous) == 0);
    struct itimerval timer = {0};
    timer.it_value.tv_usec = 30000;
    assert(setitimer(ITIMER_REAL, &timer, NULL) == 0);
    put32(28, 1000);
    // EINTR does not become a fatal VM error; V++ reevaluates its deadline.
    assert(vpp_native_os_process(1, CONTROL) == 0);
    assert(sigaction(SIGALRM, &previous, NULL) == 0);
    assert(vpp_native_os_process(4, CONTROL) == 0);
    assert(open_fd_count() == original_fds);

    for (int round = 0; round < 40; ++round) {
        assert(spawn_process(first, NULL) == 1);
        memset(out, 0, sizeof out);
        memset(err, 0, sizeof err);
        drain(out, sizeof out, err, sizeof err);
        assert(vpp_native_os_process(3, CONTROL) == 7);
        assert(vpp_native_os_process(4, CONTROL) == 0);
    }
    assert(open_fd_count() == original_fds);
    puts("VM ASM PROCESS OK");
    return 0;
}
