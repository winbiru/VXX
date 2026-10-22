#include <chrono>
#include <cerrno>
#include <filesystem>
#include <iostream>
#include <string>
#include <unordered_set>
#include <vector>

#include "vpp/runtime/error.h"
#include "vpp/runtime/file_runtime.h"
#if !defined(_WIN32)
#include <fcntl.h>
#include <unistd.h>
#include "vpp/runtime/file_machine.h"
#endif

namespace {
using namespace vietvm::runtime;
int failures = 0;
void expect(bool condition, const char *message) {
    if (!condition) { std::cerr << message << '\n'; ++failures; }
}
void checkFiles(const std::filesystem::path &root) {
    constexpr auto open = OP_VM_MACHINE_FILE_OPEN, read = OP_VM_MACHINE_FILE_READ;
    constexpr auto write = OP_VM_MACHINE_FILE_WRITE, close = OP_VM_MACHINE_FILE_CLOSE;
    constexpr auto seek = OP_VM_MACHINE_FILE_SEEK, tell = OP_VM_MACHINE_FILE_TELL;
    const std::unordered_set<std::string> grants = {
        "machine.file.open", "machine.file.read", "machine.file.write", "machine.file.close",
        "machine.file.seek", "machine.file.tell", "machine.file.error", "machine.file.remove"};
    FileMachineState owner, other;
    const auto call = [&](Opcode operation, const std::vector<StackValue> &args) {
        return executeFileMachinePrimitive(operation, args, grants, owner);
    };
    const auto rejects = [&](Opcode operation, const std::vector<StackValue> &args,
                             FileMachineState *state = nullptr) {
        try { (void)executeFileMachinePrimitive(operation, args, grants, state ? *state : owner); }
        catch (const RuntimeError &) { return true; }
        return false;
    };
    const auto path = make_string_value((root / std::filesystem::u8path("tệp-cà-phê.bin")).u8string());
    bool permissionRejected = false;
    try { (void)executeFileMachinePrimitive(open, {path, 1}, {}, owner); }
    catch (const RuntimeError &) { permissionRejected = true; }
    expect(permissionRejected, "open must require its own grant");
    expect(rejects(open, {path, 3}), "invalid mode must be rejected before opening");
    expect(rejects(open, {make_string_value(std::string("a\0b", 3)), 1}), "NUL path must be rejected");
    expect(rejects(open, {make_string_value(std::string(1, char(0xff))), 1}), "invalid UTF-8 path must be rejected");
    const auto token = call(open, {path, 1});
    expect(std::holds_alternative<int>(token) && owner.files.size() == 1,
           "raw files must never create a FILE stream");
    if (!std::holds_alternative<int>(token)) return;
    expect(rejects(close, {token}, &other), "tokens must be isolated by VM");
    expect(rejects(read, {token}), "arity must be checked before accessing arguments");
    const auto payload = make_list_value({0, 255, 65, 66});
    expect(rejects(write, {token, payload, 3}), "buffer length must match count");
    expect(rejects(write, {token, make_list_value({256}), 1}), "input byte range must be checked");
    expect(rejects(write, {token, make_list_value({make_string_value("A")}), 1}), "input byte type must be checked");
    expect(rejects(write, {token, payload, -1}), "negative lengths must be rejected");
    expect(rejects(write, {token, make_list_value({}), 0}), "zero length native I/O must be rejected");
    expect(call(write, {token, payload, 4}) == make_int_value(4), "binary write must preserve byte count");
    expect(std::get<AbiInteger>(call(tell, {token})).signedValue == 4, "tell must report the OS cursor");
    const auto big = call(seek, {token, make_string_value("4294967296"), 0});
    expect(std::get<AbiInteger>(big).signedValue == INT64_C(4294967296), "ASM seek must preserve all 64 bits");
    expect(rejects(seek, {token, 0, 3}), "seek origin must be checked");
    expect(std::get<AbiInteger>(call(seek, {token, 0, 0})).signedValue == 0, "seek must reset position");
    expect(call(close, {token}) == make_int_value(0), "close must succeed");
    expect(rejects(close, {token}), "closed tokens must be invalidated");
    const auto reader = call(open, {path, 0});
    expect(std::get<int>(reader) != std::get<int>(token), "token generations must not be reused");
    const auto buffer = make_list_value({9, 9, 9, 9, 9, 9});
    expect(call(read, {reader, buffer, 6}) == make_int_value(4), "short reads must return their actual length");
    const auto &bytes = std::get<ListHandle>(buffer)->elements;
    expect(bytes[0] == make_int_value(0) && bytes[1] == make_int_value(255) &&
           bytes[4] == make_int_value(9), "read must update only transferred bytes");
    expect(call(read, {reader, buffer, 6}) == make_int_value(0), "EOF must return zero");
    expect(call(write, {reader, payload, 4}) == make_int_value(-1) && owner.lastError == EBADF,
           "write on a reader must report EBADF");
    expect(call(OP_VM_MACHINE_FILE_ERROR, {0}) == make_int_value(EBADF), "machine errno must be VM-owned");
    expect(call(OP_VM_MACHINE_FILE_ERROR, {1}) == make_int_value(EINTR) && owner.lastError == EBADF,
           "querying EINTR must preserve the error snapshot");
    call(close, {reader});
    const auto appender = call(open, {path, 2});
    call(seek, {appender, 0, 0});
    expect(call(write, {appender, make_list_value({67}), 1}) == make_int_value(1), "append must succeed after seek");
    expect(std::get<AbiInteger>(call(tell, {appender})).signedValue == 5, "append must write at EOF");
#if !defined(_WIN32)
    const int appenderFd = static_cast<int>(owner.files.at(std::get<int>(appender)).descriptor);
#endif
    owner.closeAll();
    expect(owner.files.empty() && rejects(tell, {appender}), "reset must invalidate raw tokens");
#if !defined(_WIN32)
    errno = 0;
    expect(::fcntl(appenderFd, F_GETFD) == -1 && errno == EBADF, "reset must close the actual OS descriptor");
#endif
    const auto missing = call(open, {make_string_value((root / "missing").u8string()), 0});
    expect(std::holds_alternative<std::monostate>(missing) && owner.lastError == ENOENT,
           "ASM errors must be translated to the VM errno snapshot");
    expect(call(OP_VM_MACHINE_FILE_REMOVE, {path}) == make_int_value(0), "ASM unlink must remove the file");
    expect(call(OP_VM_MACHINE_FILE_REMOVE, {path}) == make_int_value(-1) && owner.lastError == ENOENT,
           "missing file removal must preserve errno");
#if !defined(_WIN32)
    // The kernel may accept fewer bytes than requested on a nonblocking pipe.
    // This checks the machine ABI's short-write contract without libc stdio.
    int pipes[2];
    if (::pipe(pipes) == 0) {
        ::fcntl(pipes[1], F_SETFL, O_NONBLOCK);
        std::vector<unsigned char> large(65536, 42);
        (void)file_machine::write(pipes[1], large.data(), 1);
        bool sawShort = false;
        for (int i = 0; i < 256; ++i) {
            const auto n = file_machine::write(pipes[1], large.data(), large.size());
            if (n.value > 0 && n.value < static_cast<std::int64_t>(large.size())) sawShort = true;
            if (n.value < 0) { expect(n.error == EAGAIN, "full pipe must return EAGAIN"); break; }
        }
        expect(sawShort, "ASM write must return a partial pipe write");
        ::close(pipes[0]); ::close(pipes[1]);
    }
    const auto bad = file_machine::read(-1, nullptr, 1);
    expect(bad.value == -1 && bad.error == EBADF, "ASM error flag must preserve EBADF");
#endif
}
} // namespace
int main() {
    const auto root = std::filesystem::temp_directory_path() /
        ("vpp-file-io-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    if (!std::filesystem::create_directory(root)) return 1;
    try { checkFiles(root); }
    catch (const std::exception &e) { std::cerr << e.what() << '\n'; ++failures; }
    std::filesystem::remove_all(root);
    if (failures) return 1;
    std::cout << "V++ file I/O / machine ABI: PASS\n";
}
