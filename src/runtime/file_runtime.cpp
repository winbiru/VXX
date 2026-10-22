#include "vpp/runtime/file_runtime.h"
#include "vpp/runtime/file_machine.h"
#include "vpp/runtime/resource_token.h"
#include "vpp/runtime/error.h"
#include "vpp/core/text.h"
#include <cerrno>
#include <charconv>
#include <climits>
#include <limits>
#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <fcntl.h>
#include <unistd.h>
#endif

namespace vietvm::runtime {
namespace {
RuntimeError machineError(const std::string &message) {
    return RuntimeError("máy tệp: " + message, RuntimeErrorKind::CallBoundary);
}
const std::string &checkedPath(const StackValue &argument) {
    const auto *path = std::get_if<std::string>(&argument);
    if (!path || path->find('\0') != std::string::npos || !core::isValidUtf8(*path))
        throw machineError("đường dẫn phải là UTF-8 không chứa NUL");
    return *path;
}
std::int64_t offset64(const StackValue &value) {
    if (const auto *n = std::get_if<int>(&value)) return *n;
    if (const auto *s = std::get_if<std::string>(&value)) {
        std::int64_t n = 0;
        const auto parsed = std::from_chars(s->data(), s->data() + s->size(), n);
        if (!s->empty() && parsed.ec == std::errc{} && parsed.ptr == s->data() + s->size()) return n;
    }
    if (const auto *n = std::get_if<AbiInteger>(&value)) {
        if (n->isSigned) return n->signedValue;
        if (n->unsignedValue <= INT64_MAX) return static_cast<std::int64_t>(n->unsignedValue);
    }
    throw machineError("độ lệch phải là số nguyên i64 chính xác");
}
#if defined(_WIN32)
int windowsError(DWORD error) {
    switch (error) {
        case ERROR_FILE_NOT_FOUND: case ERROR_PATH_NOT_FOUND: return ENOENT;
        case ERROR_ALREADY_EXISTS: case ERROR_FILE_EXISTS: return EEXIST;
        case ERROR_ACCESS_DENIED: case ERROR_SHARING_VIOLATION: return EACCES;
        case ERROR_DIRECTORY: return ENOTDIR;
        case ERROR_INVALID_PARAMETER: case ERROR_INVALID_NAME: case ERROR_BAD_PATHNAME: return EINVAL;
        default: return EIO;
    }
}
std::wstring widePath(const std::string &path) {
    if (path.empty()) return {};
    if (path.size() > INT_MAX) throw machineError("đường dẫn quá dài");
    const int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path.data(), static_cast<int>(path.size()), nullptr, 0);
    if (n <= 0) throw machineError("đường dẫn không phải UTF-8");
    std::wstring result(static_cast<std::size_t>(n), 0);
    if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path.data(), static_cast<int>(path.size()), result.data(), n) != n)
        throw machineError("không thể chuyển đường dẫn sang UTF-16");
    return result;
}
#endif
int closeRawFile(std::intptr_t fd) noexcept {
#if defined(_WIN32)
    return CloseHandle(reinterpret_cast<HANDLE>(fd)) ? 0 : windowsError(GetLastError());
#else
    // Never retry close after EINTR: the descriptor may already be released.
    return file_machine::close(static_cast<int>(fd)).error;
#endif
}
struct MachineResult { StackValue value; int error; };
MachineResult perform(Opcode opcode, const std::vector<StackValue> &arguments, FileMachineState &state) {
    const bool open = opcode == OP_VM_MACHINE_FILE_OPEN;
    const bool read = opcode == OP_VM_MACHINE_FILE_READ;
    const bool write = opcode == OP_VM_MACHINE_FILE_WRITE;
    const bool close = opcode == OP_VM_MACHINE_FILE_CLOSE;
    const bool seek = opcode == OP_VM_MACHINE_FILE_SEEK;
    const bool tell = opcode == OP_VM_MACHINE_FILE_TELL;
    const bool remove = opcode == OP_VM_MACHINE_FILE_REMOVE;
    const auto integer = [&](std::size_t index) {
        const auto *value = std::get_if<int>(&arguments[index]);
        if (!value) throw machineError("primitive tệp yêu cầu i32");
        return *value;
    };
    if (open) {
        const auto &path = checkedPath(arguments[0]);
        if (!core::isValidUtf8(path)) throw machineError("đường dẫn tệp không phải UTF-8 hợp lệ");
        const int mode = integer(1);
        if (mode < 0 || mode > 2) throw machineError("raw_open: chế độ phải trong 0..2");
        const auto token = nextOpaqueResourceToken();
        if (token > INT32_MAX) throw machineError("hết định danh tệp máy");
        std::intptr_t fd;
#if defined(_WIN32)
        HANDLE handle = CreateFileW(widePath(path).c_str(), mode == 0 ? GENERIC_READ : GENERIC_WRITE,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
            mode == 0 ? OPEN_EXISTING : mode == 1 ? CREATE_ALWAYS : OPEN_ALWAYS,
            FILE_ATTRIBUTE_NORMAL, nullptr);
        if (handle == INVALID_HANDLE_VALUE) return {make_null_value(), windowsError(GetLastError())};
        fd = reinterpret_cast<std::intptr_t>(handle);
#else
        const int flags = (mode == 0 ? O_RDONLY : O_WRONLY | O_CREAT |
            (mode == 1 ? O_TRUNC : O_APPEND)) | O_CLOEXEC;
        const auto opened = file_machine::open(path.c_str(), flags, 0666);
        if (opened.value < 0) return {make_null_value(), opened.error};
        fd = opened.value;
#endif
        try {
            state.files.emplace(static_cast<int>(token), FileMachineState::File{fd, mode});
        } catch (...) { (void)closeRawFile(fd); throw; }
        return {make_int_value(static_cast<int>(token)), 0};
    }
    if (remove) {
        const auto &path = checkedPath(arguments[0]);
#if defined(_WIN32)
        if (!DeleteFileW(widePath(path).c_str())) return {make_int_value(-1), windowsError(GetLastError())};
        return {make_int_value(0), 0};
#else
        const auto removed = file_machine::remove(path.c_str());
        return {make_int_value(static_cast<int>(removed.value)), removed.error};
#endif
    }
    const auto entry = state.files.find(integer(0));
    if (entry == state.files.end()) throw machineError("c_tệp thô đã đóng hoặc không thuộc VM");
    const auto file = entry->second;
    if (close) {
        state.files.erase(entry);
        const int error = closeRawFile(file.descriptor);
        return {make_int_value(error == 0 ? 0 : -1), error};
    }
    if (seek || tell) {
        const auto offset = tell ? 0 : offset64(arguments[1]);
        const int origin = tell ? 1 : integer(2);
        if (origin < 0 || origin > 2) throw machineError("raw_seek: gốc phải trong 0..2");
        std::int64_t position;
#if defined(_WIN32)
        LARGE_INTEGER distance{}, target{};
        distance.QuadPart = offset;
        if (!SetFilePointerEx(reinterpret_cast<HANDLE>(file.descriptor), distance, &target,
                origin == 0 ? FILE_BEGIN : origin == 1 ? FILE_CURRENT : FILE_END))
            return {make_abi_integer_value(AbiInteger::signedNumber(-1)), windowsError(GetLastError())};
        position = target.QuadPart;
#else
        if (offset < std::numeric_limits<off_t>::min() || offset > std::numeric_limits<off_t>::max())
            throw machineError("raw_seek: độ lệch ngoài miền off_t");
        const auto moved = file_machine::seek(static_cast<int>(file.descriptor), offset,
            origin == 0 ? SEEK_SET : origin == 1 ? SEEK_CUR : SEEK_END);
        if (moved.value < 0) return {make_abi_integer_value(AbiInteger::signedNumber(-1)), moved.error};
        position = moved.value;
#endif
        return {make_abi_integer_value(AbiInteger::signedNumber(position)), 0};
    }
    const auto count = integer(2);
    const auto *list = std::get_if<ListHandle>(&arguments[1]);
    if (count <= 0 || count > 65536 || !list || !*list || (*list)->elements.size() != static_cast<std::size_t>(count))
        throw machineError("primitive tệp yêu cầu đệm khớp độ dài 1..65536");
    auto &elements = (*list)->elements;
    std::vector<unsigned char> buffer(static_cast<std::size_t>(count));
    if (write) {
        for (std::size_t i = 0; i < buffer.size(); ++i) {
            const auto *byte = std::get_if<int>(&elements[i]);
            if (!byte || *byte < 0 || *byte > 255) throw machineError("raw_write: byte ngoài miền 0..255");
            buffer[i] = static_cast<unsigned char>(*byte);
        }
    }
    if ((read && file.mode != 0) || (write && file.mode == 0))
        return {make_int_value(-1), EBADF};
    int transferred;
#if defined(_WIN32)
    DWORD n = 0;
    OVERLAPPED append{};
    append.Offset = append.OffsetHigh = MAXDWORD;
    // A synchronous WriteFile with the special EOF offset preserves append
    // semantics even after seek, without a racy seek-to-end/write pair.
    const BOOL ok = read
        ? ReadFile(reinterpret_cast<HANDLE>(file.descriptor), buffer.data(), static_cast<DWORD>(count), &n, nullptr)
        : WriteFile(reinterpret_cast<HANDLE>(file.descriptor), buffer.data(), static_cast<DWORD>(count), &n,
            file.mode == 2 ? &append : nullptr);
    if (!ok) return {make_int_value(-1), windowsError(GetLastError())};
    transferred = static_cast<int>(n);
#else
    const auto n = read
        ? file_machine::read(static_cast<int>(file.descriptor), buffer.data(), buffer.size())
        : file_machine::write(static_cast<int>(file.descriptor), buffer.data(), buffer.size());
    if (n.value < 0) return {make_int_value(-1), n.error};
    transferred = static_cast<int>(n.value);
#endif
    if (read) {
        for (int i = 0; i < transferred; ++i) elements[static_cast<std::size_t>(i)] = make_int_value(buffer[static_cast<std::size_t>(i)]);
    }
    return {make_int_value(transferred), 0};}
} // namespace

bool isFileMachinePrimitive(int opcode) noexcept {
    return opcode >= OP_VM_MACHINE_FILE_OPEN && opcode <= OP_VM_MACHINE_FILE_REMOVE;
}
FileMachineState::~FileMachineState() { closeAll(); }
void FileMachineState::closeAll() noexcept {
    for (const auto &[token, file] : files) { (void)token; (void)closeRawFile(file.descriptor); }
    files.clear(); lastError = 0;
}
StackValue executeFileMachinePrimitive(Opcode opcode, const std::vector<StackValue> &arguments,
    const std::unordered_set<std::string> &capabilities, FileMachineState &state) {
    const char *operation;
    std::size_t arity;
    switch (opcode) {
        case OP_VM_MACHINE_FILE_OPEN: operation = "open"; arity = 2; break;
        case OP_VM_MACHINE_FILE_READ: operation = "read"; arity = 3; break;
        case OP_VM_MACHINE_FILE_WRITE: operation = "write"; arity = 3; break;
        case OP_VM_MACHINE_FILE_CLOSE: operation = "close"; arity = 1; break;
        case OP_VM_MACHINE_FILE_SEEK: operation = "seek"; arity = 3; break;
        case OP_VM_MACHINE_FILE_TELL: operation = "tell"; arity = 1; break;
        case OP_VM_MACHINE_FILE_ERROR: operation = "error"; arity = 1; break;
        case OP_VM_MACHINE_FILE_REMOVE: operation = "remove"; arity = 1; break;
        default: throw machineError("opcode không hợp lệ");
    }
    if (!capabilities.count(std::string("machine.file.") + operation))
        throw machineError("thiếu quyền machine.file." + std::string(operation));
    if (arguments.size() != arity) throw machineError("sai số đối số primitive");
    if (opcode == OP_VM_MACHINE_FILE_ERROR) {
        const auto *query = std::get_if<int>(&arguments[0]);
        if (!query || (*query != 0 && *query != 1)) throw machineError("truy vấn lỗi phải là 0 hoặc 1");
        return make_int_value(*query == 0 ? state.lastError : EINTR);
    }
    auto result = perform(opcode, arguments, state);
    state.lastError = result.error;
    return std::move(result.value);
}
} // namespace vietvm::runtime
