// Test driver only: the actual socket implementation is V++/ASM.
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <time.h>
#include <netinet/in.h>
#include <unistd.h>

enum { ARENA = 68162560, CTL = 8192, DATA = 8392704 };
static unsigned char *memory;
static int socket_bind_forbidden;
static volatile sig_atomic_t interrupted;
static void alarm_handler(int signal_number) {
    (void)signal_number;
    interrupted = 1;
}
static int64_t monotonic_ms(void) {
    struct timespec now;
    assert(clock_gettime(CLOCK_MONOTONIC, &now) == 0);
    return (int64_t)now.tv_sec * 1000 + now.tv_nsec / 1000000;
}
extern int32_t vpp_native_os_socket(int32_t operation, int32_t control_offset);
unsigned char *vpp_native_memory_base(void) {
    if (!memory) {
        memory = mmap(NULL, ARENA, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
        assert(memory != MAP_FAILED);
    }
    return memory;
}
static void put(unsigned offset, int32_t value) {
    memcpy(memory + CTL + offset, &value, 4);
}
static void init(void) {
    memset(memory + CTL, 0, 128);
    put(0, -1);
    put(12, 1000);
}
static int listener(int family, int type, unsigned char *ip, int *port) {
    int fd = socket(family, type, 0);
    if (fd < 0) return -1;
    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = 0;
    if (bind(fd, (struct sockaddr *)&addr, sizeof addr) != 0) {
        socket_bind_forbidden = errno == EPERM || errno == EACCES;
        close(fd);
        return -1;
    }
    socklen_t len = sizeof addr;
    assert(getsockname(fd, (struct sockaddr *)&addr, &len) == 0);
    *port = ntohs(addr.sin_port);
    ip[0] = 127; ip[1] = 0; ip[2] = 0; ip[3] = 1;
    if (type == SOCK_STREAM) assert(listen(fd, 4) == 0);
    return fd;
}
static int open_fds(void) {
    int n = 0;
    for (int fd = 0; fd < 256; fd++) {
        if (fcntl(fd, F_GETFD) >= 0 || errno != EBADF) n++;
    }
    return n;
}
int main(void) {
    assert(vpp_native_memory_base());
    assert(vpp_native_os_socket(0, -1) == -22);
    assert(vpp_native_os_socket(6, 68162560) == -22);
    assert(vpp_native_os_socket(99, CTL) == -22);
    int baseline = open_fds();
    unsigned char ip[16] = {0};
    int port;
    int server = listener(AF_INET, SOCK_STREAM, ip, &port);
    if (server < 0 && socket_bind_forbidden) {
        puts("VM ASM SOCKET SKIP: local socket bind prohibited");
        return 77;
    }
    assert(server >= 0);
    init();
    put(8, port); put(24, 4);
    memcpy(memory + CTL + 32, ip, 4);
    int client = vpp_native_os_socket(0, CTL);
    assert(client >= 0);
    assert((fcntl(client, F_GETFL) & O_NONBLOCK) == 0);
    assert(vpp_native_os_socket(6, CTL) == 0);
    int peer = accept(server, NULL, NULL);
    assert(peer >= 0);
    memcpy(memory + DATA, "hello", 5);
    put(16, DATA); put(20, 5);
    assert(vpp_native_os_socket(3, CTL) == 5);
    char output[8] = {0};
    assert(recv(peer, output, 5, 0) == 5);
    assert(memcmp(output, "hello", 5) == 0);
    assert(send(peer, "world", 5, 0) == 5);
    assert(vpp_native_os_socket(4, CTL) == 5);
    assert(memcmp(memory + DATA, "world", 5) == 0);
    assert(vpp_native_os_socket(5, CTL) == 0);
    assert(vpp_native_os_socket(5, CTL) < 0);
    close(peer); close(server);

    server = listener(AF_INET, SOCK_DGRAM, ip, &port);
    assert(server >= 0);
    init(); put(8, port); put(24, 4); put(4, 1);
    memcpy(memory + CTL + 32, ip, 4);
    assert(vpp_native_os_socket(0, CTL) >= 0);
    memcpy(memory + DATA, "udp", 3);
    put(16, DATA); put(20, 3);
    assert(vpp_native_os_socket(3, CTL) == 3);
    struct sockaddr_storage from;
    socklen_t fromlen = sizeof from;
    memset(output, 0, sizeof output);
    assert(recvfrom(server, output, 3, 0, (struct sockaddr *)&from, &fromlen) == 3);
    assert(memcmp(output, "udp", 3) == 0);
    assert(sendto(server, "yes", 3, 0, (struct sockaddr *)&from, fromlen) == 3);
    assert(vpp_native_os_socket(4, CTL) == 3);
    assert(memcmp(memory + DATA, "yes", 3) == 0);
    assert(vpp_native_os_socket(5, CTL) == 0);
    close(server);

    // Darwin AF_INET6 is 30 (Linux is 10). Verify address layout and
    // the architecture-specific family number with a real ::1 handshake.
    int server6 = socket(AF_INET6, SOCK_STREAM, 0);
    if (server6 >= 0) {
        struct sockaddr_in6 ipv6 = {0};
        ipv6.sin6_family = AF_INET6;
        ipv6.sin6_addr = in6addr_loopback;
        ipv6.sin6_port = 0;
        if (bind(server6, (struct sockaddr *)&ipv6, sizeof ipv6) == 0) {
            socklen_t ipv6_len = sizeof ipv6;
            assert(getsockname(server6, (struct sockaddr *)&ipv6, &ipv6_len) == 0);
            assert(listen(server6, 4) == 0);
            init(); put(8, ntohs(ipv6.sin6_port)); put(24, 16);
            memory[CTL + 47] = 1; // ::1
            assert(vpp_native_os_socket(0, CTL) >= 0);
            int peer6 = accept(server6, NULL, NULL);
            assert(peer6 >= 0);
            memcpy(memory + DATA, "v6", 2);
            put(16, DATA); put(20, 2);
            assert(vpp_native_os_socket(3, CTL) == 2);
            assert(recv(peer6, output, 2, 0) == 2);
            assert(memcmp(output, "v6", 2) == 0);
            assert(vpp_native_os_socket(5, CTL) == 0);
            close(peer6);
        } else {
            assert(errno == EAFNOSUPPORT || errno == EADDRNOTAVAIL ||
                   errno == EACCES || errno == EPERM);
        }
        close(server6);
    } else {
        assert(errno == EAFNOSUPPORT || errno == EPROTONOSUPPORT ||
               errno == EACCES || errno == EPERM);
    }

    // Assembly-created listener and accepted descriptor.
    int probe = listener(AF_INET, SOCK_STREAM, ip, &port);
    assert(probe >= 0); close(probe);
    // A failed asynchronous connect must close its temporary descriptor and
    // preserve the OS error, including after O_NONBLOCK/poll/SO_ERROR.
    init(); put(8, port); put(24, 4);
    memcpy(memory + CTL + 32, ip, 4);
    assert(vpp_native_os_socket(0, CTL) == -ECONNREFUSED);
    int failed_fd;
    memcpy(&failed_fd, memory + CTL, sizeof failed_fd);
    assert(failed_fd == -1);
    assert(open_fds() == baseline);
    init(); put(8, port); put(20, 8);
    int serverfd = vpp_native_os_socket(1, CTL);
    assert(serverfd >= 0);
    // No incoming connection: accept must expire and retain the listener FD.
    put(12, 25);
    assert(vpp_native_os_socket(2, CTL) == -ETIMEDOUT);
    int retained_fd;
    memcpy(&retained_fd, memory + CTL, sizeof retained_fd);
    assert(retained_fd == serverfd);
    // SIGALRM must not leak EINTR to the guest or restart the full timeout.
    struct sigaction handler = {0}, previous_handler;
    handler.sa_handler = alarm_handler;
    sigemptyset(&handler.sa_mask);
    assert(sigaction(SIGALRM, &handler, &previous_handler) == 0);
    struct itimerval timer = {0};
    timer.it_value.tv_usec = 15000;
    assert(setitimer(ITIMER_REAL, &timer, NULL) == 0);
    put(12, 100);
    int64_t started = monotonic_ms();
    assert(vpp_native_os_socket(2, CTL) == -ETIMEDOUT);
    int64_t elapsed = monotonic_ms() - started;
    assert(interrupted == 1);
    assert(elapsed >= 60 && elapsed < 500);
    assert(sigaction(SIGALRM, &previous_handler, NULL) == 0);
    memcpy(&retained_fd, memory + CTL, sizeof retained_fd);
    assert(retained_fd == serverfd);
    int connectfd = socket(AF_INET, SOCK_STREAM, 0);
    assert(connectfd >= 0);
    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET; addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons((uint16_t)port);
    assert(connect(connectfd, (struct sockaddr *)&addr, sizeof addr) == 0);
    put(0, serverfd);
    int accepted = vpp_native_os_socket(2, CTL);
    assert(accepted >= 0);
    struct timeval accepted_timeout = {0};
    socklen_t opt_len = sizeof accepted_timeout;
    assert(getsockopt(accepted, SOL_SOCKET, SO_RCVTIMEO,
                      &accepted_timeout, &opt_len) == 0);
    assert(accepted_timeout.tv_sec > 0 || accepted_timeout.tv_usec > 0);
    assert(vpp_native_os_socket(5, CTL) == 0);
    put(0, serverfd);
    assert(vpp_native_os_socket(5, CTL) == 0);
    close(connectfd);

    // No user-controlled address or offset is used unchecked.
    init(); put(16, ARENA-2); put(20, 3); put(0, 0);
    assert(vpp_native_os_socket(3, CTL) == -22);
    assert(open_fds() == baseline);

    // The socket boundary must never leak descriptors across repeated cycles.
    for (int round = 0; round < 40; round++) {
        init(); put(8, 9); put(24, 4); put(4, 1);
        memcpy(memory + CTL + 32, ip, 4);
        assert(vpp_native_os_socket(0, CTL) >= 0);
        assert(vpp_native_os_socket(5, CTL) == 0);
    }
    assert(open_fds() == baseline);
    puts("VM ASM SOCKET OK");
    return 0;
}
